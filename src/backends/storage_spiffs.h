/*
 * ESP-IDF UART Filebridge - SPIFFS Storage Backend
 * 
 * SPIFFS (Serial Peripheral Interface Flash File System) backend for internal flash.
 * 
 * IMPORTANT LIMITATIONS:
 * - NO DIRECTORY SUPPORT: SPIFFS has a flat namespace (all files in single namespace)
 * - create_directory() returns ESP_ERR_NOT_SUPPORTED
 * - list_directory() enumerates ALL files regardless of path parameter
 * - File paths like "/subdir/file.txt" are treated as single filename
 * 
 * These limitations are inherent to SPIFFS architecture and documented clearly.
 * 
 * Version: 2.0.0
 * Date: 2026-10-13
 */

#pragma once

#include "../internal/storage_backend.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * SPIFFS storage backend implementation.
 * 
 * Provides access to ESP32 internal flash formatted with SPIFFS.
 * Follows StorageLittleFS pattern but with directory support disabled.
 * 
 * Capabilities:
 * - CAP_READ, CAP_WRITE: Standard file operations
 * - CAP_HASH: CRC32 calculation
 * - CAP_FORMAT: Filesystem formatting
 * - CAP_RENAME, CAP_COPY: File operations
 * 
 * NOT Supported:
 * - CAP_DIRECTORIES: SPIFFS is flat namespace
 * - CAP_RECURSIVE_DELETE: No directories to delete
 * - CAP_TIMESTAMPS: SPIFFS doesn't track timestamps
 * - CAP_LARGE_FILES: Optimized for small files
 * 
 * Example Usage:
 *   StorageSpiffs spiffs("/spiffs", "storage", 5, true);
 *   spiffs.mount();
 *   FILE* file;
 *   spiffs.open_file("/config.txt", "w", &file);
 *   fprintf(file, "data");
 *   fclose(file);
 *   spiffs.unmount();
 */
class StorageSpiffs : public StorageBackend {
public:
    /**
     * Construct SPIFFS backend.
     * 
     * @param mount_point        VFS mount point (e.g., "/spiffs")
     * @param partition_label    Flash partition label (e.g., "storage")
     * @param max_files          Maximum open files (1-10, default 5)
     * @param format_if_mount_fails  Auto-format on mount failure
     */
    StorageSpiffs(const char* mount_point, 
                  const char* partition_label,
                  int max_files = 5,
                  bool format_if_mount_fails = false);
    
    virtual ~StorageSpiffs();
    
    // Lifecycle Management
    esp_err_t mount() override;
    esp_err_t unmount() override;
    bool is_mounted() const override;
    
    // Backend Information
    uint32_t get_capabilities() const override;
    const char* get_type_name() const override { return "SPIFFS"; }
    const char* get_mount_point() const override { return m_mount_point; }
    StorageType get_type() const override { return StorageType::SPIFFS; }
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
    
    // Directory Operations (SPIFFS LIMITATIONS)
    esp_err_t create_directory(const char* path) override;
    esp_err_t delete_directory_recursive(const char* path) override;
    esp_err_t list_directory(const char* path,
                            void (*callback)(const char* name, uint64_t size,
                                           bool is_dir, uint32_t timestamp,
                                           void* user_data),
                            void* user_data) override;
    
    // Advanced Operations
    esp_err_t format() override;

private:
    // Configuration
    char m_mount_point[64];       /**< VFS mount point */
    char m_partition_label[32];   /**< Flash partition label */
    int m_max_files;              /**< Maximum open files */
    bool m_format_if_mount_fails; /**< Auto-format flag */
    
    // State
    bool m_mounted;               /**< Mount status */
    SemaphoreHandle_t m_mutex;    /**< Thread safety mutex */
    
    // Helper Methods
    void resolve_path(const char* relative_path, char* absolute_path, size_t max_len) const;
    bool is_root_path(const char* path) const;
};
