/*
 * ESP-IDF UART Filebridge - Flash/FAT Storage Backend
 * 
 * Internal flash backend using ESP-IDF wear leveling and VFS FAT filesystem.
 * Optimized for NOR flash with wear leveling for extended lifetime.
 * 
 * Features:
 * - FAT filesystem on wear-leveled partition
 * - Automatic wear leveling
 * - Format support
 * - Configurable partition label
 * - Optional compression support (Phase 3)
 * 
 * Limitations:
 * - Max file size: 4GB (FAT32 limit)
 * - Slower than SD card (flash write ~100 KB/s vs SD ~10 MB/s)
 * - Limited write endurance (use compression to reduce wear)
 * 
 * Version: 2.0.0
 * Date: 2026-10-12
 */

#pragma once

#include "../internal/storage_backend.h"
#include "esp_partition.h"
#include "wear_levelling.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <cstdint>

/**
 * Flash/FAT Storage Backend Implementation.
 * 
 * Manages internal flash partition with wear leveling and FAT filesystem.
 * All operations are thread-safe via internal mutex.
 * 
 * Example usage:
 * @code
 * StorageFlashFAT flash("/flash", "storage");
 * esp_err_t ret = flash.mount();
 * if (ret == ESP_OK) {
 *     // Use flash storage
 * }
 * @endcode
 * 
 * @note Requires partition table entry with type=data, subtype=fat
 * @note Partition must be formatted with FAT before first use
 * @warning Flash has limited write cycles - use compression to extend life
 */
class StorageFlashFAT : public StorageBackend {
public:
    /**
     * Constructor.
     * 
     * @param mount_point VFS mount point (e.g., "/flash")
     * @param partition_label Partition label from partition table
     * @param max_files Maximum number of simultaneously open files (default: 5)
     * 
     * @note Does NOT mount automatically - call mount() explicitly
     * @note mount_point and partition_label are copied internally
     */
    StorageFlashFAT(const char* mount_point, 
                    const char* partition_label,
                    int max_files = 5);
    
    /**
     * Destructor.
     * 
     * Unmounts filesystem if mounted and releases all resources.
     * Thread-safe cleanup guaranteed.
     */
    ~StorageFlashFAT();
    
    // Disable copy (contains mutex and handles)
    StorageFlashFAT(const StorageFlashFAT&) = delete;
    StorageFlashFAT& operator=(const StorageFlashFAT&) = delete;
    
    /* ========================================================================
     * StorageBackend Interface Implementation
     * ====================================================================== */
    
    // Lifecycle Management
    esp_err_t mount() override;
    esp_err_t unmount() override;
    bool is_mounted() const override;
    esp_err_t format() override;
    
    // Backend Information
    const char* get_mount_point() const override { return m_mount_point; }
    StorageType get_type() const override { return StorageType::FLASH_FAT; }
    const char* get_type_name() const override { return "Flash/FAT"; }
    uint32_t get_capabilities() const override;
    esp_err_t get_backend_info(StorageBackendInfo* info) override;
    
    // Space Information
    esp_err_t get_space_info(uint64_t* total_bytes, uint64_t* free_bytes) override;
    
    // File Operations
    esp_err_t open_file(const char* path, const char* mode, FILE** out) override;
    esp_err_t stat_file(const char* path, uint64_t* size, 
                       uint32_t* timestamp, bool* is_dir) override;
    esp_err_t delete_file(const char* path) override;
    esp_err_t rename_file(const char* old_path, const char* new_path) override;
    esp_err_t copy_file(const char* src_path, const char* dst_path) override;
    esp_err_t hash_file(const char* path, uint32_t* hash) override;
    
    // Directory Operations
    esp_err_t create_directory(const char* path) override;
    esp_err_t list_directory(const char* path,
                            void (*callback)(const char* name, uint64_t size,
                                           bool is_dir, uint32_t timestamp,
                                           void* user_data),
                            void* user_data) override;
    esp_err_t delete_directory_recursive(const char* path) override;
    
private:
    
private:
    /* ========================================================================
     * Private Members
     * ====================================================================== */
    
    char m_mount_point[64];      ///< VFS mount point
    char m_partition_label[32];  ///< Partition label
    int m_max_files;             ///< Max open files
    bool m_mounted;              ///< Mount state
    wl_handle_t m_wl_handle;     ///< Wear leveling handle
    SemaphoreHandle_t m_mutex;   ///< Thread safety
    
    /* ========================================================================
     * Private Helper Methods
     * ====================================================================== */
    
    /**
     * Initialize wear leveling for partition.
     * 
     * Finds partition by label and initializes wear leveling driver.
     * 
     * @return ESP_OK on success, ESP_ERR_NOT_FOUND if partition not found
     */
    esp_err_t init_wear_leveling();
    
    /**
     * Resolve relative path to absolute VFS path.
     * 
     * @param relative_path Path relative to mount point (e.g., "/file.txt")
     * @param absolute_path Output buffer for absolute path
     * @param max_len Size of absolute_path buffer
     * 
     * @note Thread-safe (no state modification)
     */
    void resolve_path(const char* relative_path, char* absolute_path, size_t max_len) const;
    
    /**
     * Recursive directory delete implementation (internal).
     * 
     * @param path Directory to delete
     * @return ESP_OK on success
     * 
     * @note Caller must hold mutex
     * @warning Dangerous - deletes all contents
     */
    esp_err_t delete_directory_recursive_impl(const char* path);
    
    /**
     * Check if path is root of mount point.
     * 
     * @param path Path to check
     * @return true if path is "/" or empty
     */
    bool is_root_path(const char* path) const;
};
