/*
 * ESP-IDF UART Filebridge - SD/FAT Storage Backend
 * 
 * SD card backend using ESP-IDF SDMMC driver and VFS FAT filesystem.
 * Supports high-speed 4-bit SDMMC mode on ESP32, ESP32-S3, ESP32-C3, ESP32-P4.
 * 
 * Hardware Support:
 * - ESP32-P4: Includes on-chip LDO power control (CONFIG_UART_FILEBRIDGE_P4_LDO_ENABLE)
 * - Other targets: External power assumed
 * 
 * Features:
 * - FAT32/exFAT support
 * - Large file support (>4GB on exFAT)
 * - Hardware CRC via SDMMC controller
 * - Configurable max open files
 * - Format support
 * 
 * Version: 2.0.0
 * Date: 2026-10-12
 */

#pragma once

#include "../internal/storage_backend.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <cstdint>

/**
 * SD/FAT Storage Backend Implementation.
 * 
 * Manages SD card mounting, file operations, and power control (ESP32-P4).
 * All operations are thread-safe via internal mutex.
 * 
 * Lifecycle:
 * 1. Construct with mount point (e.g., "/sd")
 * 2. mount() to initialize SD card and mount FAT filesystem
 * 3. File operations via StorageBackend interface
 * 4. unmount() before destruction or card removal
 * 5. Destructor unmounts if still mounted
 * 
 * Example:
 *   StorageSDFAT sd("/sd");
 *   if (sd.mount() == ESP_OK) {
 *       FILE* file;
 *       sd.open_file("/test.txt", "w", &file);
 *       fprintf(file, "Hello SD!");
 *       fclose(file);
 *       sd.unmount();
 *   }
 * 
 * Thread Safety:
 * All public methods are thread-safe. Multiple tasks can call mount/unmount
 * and perform file operations concurrently. Internal mutex serializes
 * mount state changes.
 */
class StorageSDFAT : public StorageBackend {
public:
    /**
     * Construct SD/FAT backend.
     * 
     * @param mount_point  VFS mount point (e.g., "/sd")
     * 
     * @note Does not mount SD card - call mount() explicitly.
     * @note mount_point must remain valid for backend lifetime.
     */
    explicit StorageSDFAT(const char* mount_point);
    
    /**
     * Destructor - unmounts SD if still mounted.
     * 
     * Automatically unmounts SD card and frees resources. If SD is still
     * mounted, unmount() is called. ESP32-P4 LDO is powered off.
     * 
     * @warning Ensure all files are closed before destruction!
     */
    ~StorageSDFAT() override;
    
    // Disable copy and move
    StorageSDFAT(const StorageSDFAT&) = delete;
    StorageSDFAT& operator=(const StorageSDFAT&) = delete;
    StorageSDFAT(StorageSDFAT&&) = delete;
    StorageSDFAT& operator=(StorageSDFAT&&) = delete;
    
    /* ========================================================================
     * StorageBackend Interface Implementation
     * ====================================================================== */
    
    // Lifecycle
    esp_err_t mount() override;
    esp_err_t unmount() override;
    bool is_mounted() const override;
    
    // Backend Information
    uint32_t get_capabilities() const override;
    const char* get_type_name() const override;
    const char* get_mount_point() const override;
    StorageType get_type() const override;
    esp_err_t get_backend_info(StorageBackendInfo* info) override;
    
    // Space Information
    esp_err_t get_space_info(uint64_t* total_bytes, uint64_t* free_bytes) override;
    
    // File Operations
    esp_err_t open_file(const char* path, const char* mode, FILE** out) override;
    esp_err_t stat_file(const char* path, uint64_t* size, 
                       uint32_t* timestamp, bool* is_dir) override;
    esp_err_t delete_file(const char* path) override;
    esp_err_t rename_file(const char* old_path, const char* new_path) override;
    
    // Directory Operations
    esp_err_t create_directory(const char* path) override;
    esp_err_t list_directory(const char* path,
                            void (*callback)(const char* name, uint64_t size,
                                           bool is_dir, uint32_t timestamp,
                                           void* user_data),
                            void* user_data) override;
    esp_err_t delete_directory_recursive(const char* path) override;
    
    // Advanced Operations
    esp_err_t copy_file(const char* src_path, const char* dst_path) override;
    esp_err_t hash_file(const char* path, uint32_t* hash) override;
    esp_err_t format() override;
    
private:
    /**
     * Build full VFS path from relative path.
     * 
     * Combines mount point with relative path to create absolute VFS path.
     * 
     * @param relative_path  Path relative to mount point (e.g., "/file.txt")
     * @param[out] full_path      Buffer for full path (must be 512 bytes)
     * @return ESP_OK              Path built successfully
     *         ESP_ERR_INVALID_ARG  relative_path or full_path is NULL
     *         ESP_ERR_INVALID_SIZE Path would exceed 512 bytes
     * 
     * @note Not thread-safe - uses internal buffer. Caller must hold mutex if needed.
     */
    esp_err_t build_full_path(const char* relative_path, char* full_path) const;
    
    /**
     * Validate relative path format.
     * 
     * Checks for:
     * - NULL pointer
     * - Empty string
     * - ".." traversal
     * - Double slashes "//"
     * - Excessive length
     * 
     * @param path  Relative path to validate
     * @return ESP_OK              Path is valid
     *         ESP_ERR_INVALID_ARG  Path is invalid
     * 
     * @note Does not check if path exists, only format validity.
     */
    esp_err_t validate_relative_path(const char* path) const;
    
    /**
     * Cleanup partial upload files recursively.
     * 
     * Removes all files ending with ".esp-idf-uart-filebridge.part" suffix.
     * Called automatically after mount to clean up interrupted uploads.
     * 
     * @param dir_path  Directory to clean (absolute VFS path)
     * @return ESP_OK on success, ESP_FAIL if some files couldn't be deleted
     * 
     * @note Recursive - scans all subdirectories.
     */
    esp_err_t cleanup_partial_uploads_in_dir(const char* dir_path);
    
    /**
     * Recursive directory deletion implementation.
     * 
     * Helper for delete_directory_recursive(). Uses heap-allocated path buffer
     * to avoid stack overflow on deep directory trees.
     * 
     * @param path  Absolute VFS path to directory
     * @return ESP_OK on success, ESP_FAIL if some files couldn't be deleted
     */
    esp_err_t delete_directory_recursive_impl(const char* path);
    
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_UART_FILEBRIDGE_P4_LDO_ENABLE)
    /**
     * Initialize ESP32-P4 on-chip LDO for SD card power.
     * 
     * Configures and enables the on-chip LDO to provide 3.3V to SD card slot.
     * Only available on ESP32-P4.
     * 
     * @return ESP_OK on success, error code on failure
     */
    esp_err_t init_p4_ldo_power();
    
    /**
     * Shutdown ESP32-P4 on-chip LDO.
     * 
     * Powers off SD card slot via LDO. Called during unmount.
     */
    void deinit_p4_ldo_power();
#endif
    
    // Member variables
    char m_mount_point[64];             /**< VFS mount point (e.g., "/sd") */
    bool m_mounted;                     /**< Current mount state */
    bool m_owns_mount;                  /**< If true, we mounted the SD card */
    sdmmc_card_t* m_sd_card;           /**< SDMMC card handle (ESP-IDF) */
    void* m_sd_pwr_ctrl;               /**< ESP32-P4 LDO handle (opaque) */
    
    mutable SemaphoreHandle_t m_mutex;  /**< Mutex for thread-safe operations */
    
    static constexpr const char* TAG = "storage_sd_fat";  /**< Logging tag */
};
