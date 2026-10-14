/*
 * ESP-IDF UART Filebridge - SPIFFS Storage Backend Implementation
 */

#include "storage_spiffs.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>

static const char* TAG = "storage_spiffs";

/* ============================================================================
 * Constructor / Destructor
 * ========================================================================= */

StorageSpiffs::StorageSpiffs(const char* mount_point, 
                             const char* partition_label,
                             int max_files,
                             bool format_if_mount_fails)
    : m_max_files(max_files)
    , m_format_if_mount_fails(format_if_mount_fails)
    , m_mounted(false)
    , m_mutex(nullptr)
{
    // Copy mount point
    strncpy(m_mount_point, mount_point, sizeof(m_mount_point) - 1);
    m_mount_point[sizeof(m_mount_point) - 1] = '\0';
    
    // Copy partition label
    strncpy(m_partition_label, partition_label, sizeof(m_partition_label) - 1);
    m_partition_label[sizeof(m_partition_label) - 1] = '\0';
    
    // Create mutex for thread safety
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex for %s", m_mount_point);
    }
}

StorageSpiffs::~StorageSpiffs() {
    // Unmount if still mounted
    if (m_mounted) {
        unmount();
    }
    
    // Delete mutex
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

/* ============================================================================
 * Mount / Unmount Operations
 * ========================================================================= */

esp_err_t StorageSpiffs::mount() {
    if (!m_mutex) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    // Already mounted?
    if (m_mounted) {
        ESP_LOGW(TAG, "SPIFFS already mounted at %s", m_mount_point);
        xSemaphoreGive(m_mutex);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Mounting SPIFFS partition '%s' at %s", m_partition_label, m_mount_point);
    
    // Configure SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = m_mount_point,
        .partition_label = m_partition_label,
        .max_files = static_cast<size_t>(m_max_files),
        .format_if_mount_failed = m_format_if_mount_fails
    };
    
    // Mount SPIFFS
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        
        // Provide helpful error messages
        if (ret == ESP_FAIL) {
            ESP_LOGW(TAG, "Partition may need formatting. Call format() first.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Partition '%s' not found in partition table", m_partition_label);
        }
        
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    m_mounted = true;
    
    // Log filesystem info
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(m_partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: total=%zu KB, used=%zu KB, free=%zu KB",
                 total / 1024, used / 1024, (total - used) / 1024);
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted successfully at %s", m_mount_point);
    }
    
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

esp_err_t StorageSpiffs::unmount() {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!m_mounted) {
        xSemaphoreGive(m_mutex);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Unmounting SPIFFS from %s", m_mount_point);
    
    // Unmount SPIFFS
    esp_err_t ret = esp_vfs_spiffs_unregister(m_partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(ret));
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    m_mounted = false;
    ESP_LOGI(TAG, "SPIFFS unmounted from %s", m_mount_point);
    
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

bool StorageSpiffs::is_mounted() const {
    return m_mounted;
}

esp_err_t StorageSpiffs::format() {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGW(TAG, "Formatting SPIFFS partition '%s' - ALL DATA WILL BE LOST", m_partition_label);
    
    // Must unmount before formatting
    bool was_mounted = m_mounted;
    if (was_mounted) {
        esp_err_t ret = esp_vfs_spiffs_unregister(m_partition_label);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unmount before format: %s", esp_err_to_name(ret));
            xSemaphoreGive(m_mutex);
            return ret;
        }
        m_mounted = false;
    }
    
    // Format partition
    esp_err_t ret = esp_spiffs_format(m_partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    ESP_LOGI(TAG, "SPIFFS partition formatted successfully");
    
    // Remount if it was mounted before
    if (was_mounted) {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = m_mount_point,
            .partition_label = m_partition_label,
            .max_files = static_cast<size_t>(m_max_files),
            .format_if_mount_failed = false
        };
        
        ret = esp_vfs_spiffs_register(&conf);
        if (ret == ESP_OK) {
            m_mounted = true;
            ESP_LOGI(TAG, "Remounted after format at %s", m_mount_point);
        } else {
            ESP_LOGW(TAG, "Failed to remount after format: %s", esp_err_to_name(ret));
        }
    }
    
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

/* ============================================================================
 * Backend Information
 * ========================================================================= */

uint32_t StorageSpiffs::get_capabilities() const {
    return CAP_READ |
           CAP_WRITE |
           CAP_RENAME |
           CAP_COPY |
           CAP_HASH |
           CAP_FORMAT;
    
    // NOT supported:
    // - CAP_DIRECTORIES (SPIFFS is flat namespace)
    // - CAP_RECURSIVE_DELETE (no directories)
    // - CAP_TIMESTAMPS (SPIFFS doesn't track timestamps)
    // - CAP_LARGE_FILES (optimized for small files)
    // - CAP_COMPRESSION (not implemented yet)
}

esp_err_t StorageSpiffs::get_space_info(uint64_t* total_bytes, uint64_t* free_bytes) {
    if (!m_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!total_bytes || !free_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get SPIFFS info
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(m_partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    *total_bytes = static_cast<uint64_t>(total);
    *free_bytes = static_cast<uint64_t>(total - used);
    
    return ESP_OK;
}

/* ============================================================================
 * File Operations
 * ========================================================================= */

esp_err_t StorageSpiffs::open_file(const char* path, const char* mode, FILE** out) {
    if (!m_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !mode || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    *out = fopen(full_path, mode);
    if (!*out) {
        ESP_LOGE(TAG, "Failed to open %s: %s", full_path, strerror(errno));
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t StorageSpiffs::stat_file(const char* path, uint64_t* size, 
                                    uint32_t* timestamp, bool* is_dir) {
    if (!m_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (size) {
        *size = st.st_size;
    }
    
    if (timestamp) {
        // SPIFFS doesn't support timestamps
        *timestamp = 0;
    }
    
    if (is_dir) {
        // SPIFFS is flat - no directories
        *is_dir = false;
    }
    
    return ESP_OK;
}

esp_err_t StorageSpiffs::delete_file(const char* path) {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!m_mounted) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    ESP_LOGD(TAG, "Deleting file: %s", full_path);
    
    if (unlink(full_path) != 0) {
        ESP_LOGE(TAG, "Failed to delete %s: %s", full_path, strerror(errno));
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "File deleted: %s", full_path);
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

esp_err_t StorageSpiffs::rename_file(const char* old_path, const char* new_path) {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!m_mounted) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!old_path || !new_path) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_old_path[256];
    char full_new_path[256];
    resolve_path(old_path, full_old_path, sizeof(full_old_path));
    resolve_path(new_path, full_new_path, sizeof(full_new_path));
    
    ESP_LOGD(TAG, "Renaming: %s -> %s", full_old_path, full_new_path);
    
    if (rename(full_old_path, full_new_path) != 0) {
        ESP_LOGE(TAG, "Rename failed: %s", strerror(errno));
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "File renamed successfully");
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

esp_err_t StorageSpiffs::copy_file(const char* src_path, const char* dst_path) {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!m_mounted) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!src_path || !dst_path) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_src_path[256];
    char full_dst_path[256];
    resolve_path(src_path, full_src_path, sizeof(full_src_path));
    resolve_path(dst_path, full_dst_path, sizeof(full_dst_path));
    
    ESP_LOGD(TAG, "Copying: %s -> %s", full_src_path, full_dst_path);
    
    // Open source
    FILE* src = fopen(full_src_path, "rb");
    if (!src) {
        ESP_LOGE(TAG, "Failed to open source: %s", strerror(errno));
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    // Open destination
    FILE* dst = fopen(full_dst_path, "wb");
    if (!dst) {
        ESP_LOGE(TAG, "Failed to open destination: %s", strerror(errno));
        fclose(src);
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    // Copy data (smaller buffer for SPIFFS optimization)
    uint8_t buffer[2048];
    size_t bytes_read;
    esp_err_t ret = ESP_OK;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        size_t bytes_written = fwrite(buffer, 1, bytes_read, dst);
        if (bytes_written != bytes_read) {
            ESP_LOGE(TAG, "Write failed during copy");
            ret = ESP_FAIL;
            break;
        }
    }
    
    // Check for read error
    if (ferror(src)) {
        ESP_LOGE(TAG, "Read failed during copy");
        ret = ESP_FAIL;
    }
    
    fclose(src);
    fclose(dst);
    
    if (ret != ESP_OK) {
        // Cleanup failed copy
        unlink(full_dst_path);
    } else {
        ESP_LOGD(TAG, "File copied successfully");
    }
    
    xSemaphoreGive(m_mutex);
    return ret;
}

esp_err_t StorageSpiffs::hash_file(const char* path, uint32_t* hash) {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!m_mounted) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!path || !hash) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    FILE* file = fopen(full_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open for hash: %s", strerror(errno));
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    // Calculate CRC32 (smaller buffer for SPIFFS)
    uint32_t crc = 0;
    uint8_t buffer[2048];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        crc = esp_crc32_le(crc, buffer, bytes_read);
    }
    
    bool had_error = ferror(file);
    fclose(file);
    
    if (had_error) {
        ESP_LOGE(TAG, "Read error during hash calculation");
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    *hash = crc;
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

/* ============================================================================
 * Directory Operations (SPIFFS LIMITATIONS)
 * ========================================================================= */

esp_err_t StorageSpiffs::create_directory(const char* path) {
    // SPIFFS does not support directories - flat namespace only
    ESP_LOGW(TAG, "create_directory() not supported by SPIFFS (flat namespace)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t StorageSpiffs::delete_directory_recursive(const char* path) {
    // SPIFFS does not support directories - flat namespace only
    ESP_LOGW(TAG, "delete_directory_recursive() not supported by SPIFFS (flat namespace)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t StorageSpiffs::list_directory(const char* path,
                                   void (*callback)(const char* name, uint64_t size,
                                                   bool is_dir, uint32_t timestamp,
                                                   void* user_data),
                                   void* user_data) {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!m_mounted) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!callback) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    // IMPORTANT: SPIFFS has flat namespace - ignore path parameter
    // Enumerate ALL files regardless of requested path
    ESP_LOGD(TAG, "Listing all SPIFFS files (path parameter ignored - flat namespace)");
    
    DIR* dir = opendir(m_mount_point);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open SPIFFS root: %s", strerror(errno));
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Get full path for stat
        char entry_path[512];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", m_mount_point, entry->d_name);
        
        struct stat st;
        uint64_t size = 0;
        
        if (stat(entry_path, &st) == 0) {
            size = st.st_size;
        }
        
        // Call callback - SPIFFS never has directories (is_dir = false)
        callback(entry->d_name, size, false, 0, user_data);
    }
    
    closedir(dir);
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

/* ============================================================================
 * Advanced Operations
 * ========================================================================= */

esp_err_t StorageSpiffs::get_backend_info(StorageBackendInfo* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(info, 0, sizeof(StorageBackendInfo));
    
    // Basic info (use snprintf to avoid truncation warning)
    snprintf(info->mount_point, sizeof(info->mount_point), "%s", m_mount_point);
    info->type = static_cast<uint8_t>(get_type());
    snprintf(info->type_name, sizeof(info->type_name), "%s", get_type_name());
    info->capabilities = get_capabilities();
    info->mounted = static_cast<uint8_t>(m_mounted);
    
    // Space info (if mounted)
    if (m_mounted) {
        get_space_info(&info->total_bytes, &info->free_bytes);
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Private Helper Methods
 * ========================================================================= */

void StorageSpiffs::resolve_path(const char* relative_path, 
                                  char* absolute_path, 
                                  size_t max_len) const {
    if (!relative_path || !absolute_path || max_len == 0) {
        return;
    }
    
    // If path starts with mount point, use as-is
    if (strncmp(relative_path, m_mount_point, strlen(m_mount_point)) == 0) {
        strncpy(absolute_path, relative_path, max_len - 1);
    } else {
        // Combine mount point + relative path
        snprintf(absolute_path, max_len, "%s%s%s",
                 m_mount_point,
                 (relative_path[0] == '/') ? "" : "/",
                 relative_path);
    }
    
    absolute_path[max_len - 1] = '\0';
}

bool StorageSpiffs::is_root_path(const char* path) const {
    if (!path) {
        return false;
    }
    
    return (path[0] == '\0' || (path[0] == '/' && path[1] == '\0'));
}
