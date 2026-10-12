/*
 * ESP-IDF UART Filebridge - Flash/FAT Storage Backend Implementation
 */

#include "storage_flash_fat.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>

static const char* TAG = "storage_flash_fat";

/* ============================================================================
 * Constructor / Destructor
 * ========================================================================= */

StorageFlashFAT::StorageFlashFAT(const char* mount_point, 
                                 const char* partition_label,
                                 int max_files)
    : m_max_files(max_files)
    , m_mounted(false)
    , m_wl_handle(WL_INVALID_HANDLE)
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

StorageFlashFAT::~StorageFlashFAT() {
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

esp_err_t StorageFlashFAT::mount() {
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
        ESP_LOGW(TAG, "Flash already mounted at %s", m_mount_point);
        xSemaphoreGive(m_mutex);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Mounting flash partition '%s' at %s", m_partition_label, m_mount_point);
    
    // Initialize wear leveling
    esp_err_t ret = init_wear_leveling();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wear leveling init failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    // Mount FAT filesystem with wear leveling
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // Don't auto-format (explicit format() call)
        .max_files = m_max_files,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };
    
    ret = esp_vfs_fat_spiflash_mount_rw_wl(m_mount_point, m_partition_label, 
                                            &mount_config, &m_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FAT: %s", esp_err_to_name(ret));
        
        // If partition not formatted, provide helpful message
        if (ret == ESP_FAIL) {
            ESP_LOGW(TAG, "Partition may need formatting. Call format() first.");
        }
        
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    m_mounted = true;
    ESP_LOGI(TAG, "Flash mounted successfully at %s (wl_handle=%d)", 
             m_mount_point, m_wl_handle);
    
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

esp_err_t StorageFlashFAT::unmount() {
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
    
    ESP_LOGI(TAG, "Unmounting flash from %s", m_mount_point);
    
    // Unmount FAT filesystem
    esp_err_t ret = esp_vfs_fat_spiflash_unmount_rw_wl(m_mount_point, m_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(ret));
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    m_mounted = false;
    m_wl_handle = WL_INVALID_HANDLE;
    ESP_LOGI(TAG, "Flash unmounted from %s", m_mount_point);
    
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

bool StorageFlashFAT::is_mounted() const {
    return m_mounted;
}

esp_err_t StorageFlashFAT::format() {
    if (!m_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGW(TAG, "Formatting flash partition '%s' - ALL DATA WILL BE LOST", m_partition_label);
    
    // Must unmount before formatting
    bool was_mounted = m_mounted;
    if (was_mounted) {
        esp_err_t ret = esp_vfs_fat_spiflash_unmount_rw_wl(m_mount_point, m_wl_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unmount before format: %s", esp_err_to_name(ret));
            xSemaphoreGive(m_mutex);
            return ret;
        }
        m_mounted = false;
        m_wl_handle = WL_INVALID_HANDLE;
    }
    
    // Initialize wear leveling if not already done
    if (m_wl_handle == WL_INVALID_HANDLE) {
        esp_err_t ret = init_wear_leveling();
        if (ret != ESP_OK) {
            xSemaphoreGive(m_mutex);
            return ret;
        }
    }
    
    // Format with FAT
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,  // Force format
        .max_files = m_max_files,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };
    
    esp_err_t ret = esp_vfs_fat_spiflash_format_rw_wl(m_partition_label, m_mount_point);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    ESP_LOGI(TAG, "Flash partition formatted successfully");
    
    // Remount if it was mounted before
    if (was_mounted) {
        ret = esp_vfs_fat_spiflash_mount_rw_wl(m_mount_point, m_partition_label,
                                                &mount_config, &m_wl_handle);
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

uint32_t StorageFlashFAT::get_capabilities() const {
    return CAP_READ |
           CAP_WRITE |
           CAP_DIRECTORIES |
           CAP_RECURSIVE_DELETE |
           CAP_RENAME |
           CAP_COPY |
           CAP_HASH |
           CAP_FORMAT;
    
    // NOT supported:
    // - CAP_LARGE_FILES (FAT32 has 4GB limit)
    // - CAP_TIMESTAMPS (FAT doesn't preserve timestamps reliably)
    // - CAP_COMPRESSION (added in Phase 3)
    // - CAP_WEAR_LEVELING (wear leveling is transparent, not a capability)
}

esp_err_t StorageFlashFAT::get_space_info(uint64_t* total_bytes, uint64_t* free_bytes) {
    if (!m_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!total_bytes || !free_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get filesystem stats
    struct statvfs stat;
    if (statvfs(m_mount_point, &stat) != 0) {
        ESP_LOGE(TAG, "statvfs failed: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    *total_bytes = (uint64_t)stat.f_blocks * stat.f_frsize;
    *free_bytes = (uint64_t)stat.f_bfree * stat.f_frsize;
    
    return ESP_OK;
}

/* ============================================================================
 * File Operations
 * ========================================================================= */

esp_err_t StorageFlashFAT::open_file(const char* path, const char* mode, FILE** out) {
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

esp_err_t StorageFlashFAT::stat_file(const char* path, uint64_t* size, 
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
        *timestamp = (uint32_t)st.st_mtime;
    }
    
    if (is_dir) {
        *is_dir = S_ISDIR(st.st_mode);
    }
    
    return ESP_OK;
}

esp_err_t StorageFlashFAT::delete_file(const char* path) {
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

esp_err_t StorageFlashFAT::rename_file(const char* old_path, const char* new_path) {
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

esp_err_t StorageFlashFAT::copy_file(const char* src_path, const char* dst_path) {
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
    
    // Copy data
    uint8_t buffer[4096];
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

esp_err_t StorageFlashFAT::hash_file(const char* path, uint32_t* hash) {
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
    
    // Calculate CRC32
    uint32_t crc = 0;
    uint8_t buffer[4096];
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
 * Directory Operations
 * ========================================================================= */

esp_err_t StorageFlashFAT::create_directory(const char* path) {
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
    
    // Can't create root
    if (is_root_path(path)) {
        xSemaphoreGive(m_mutex);
        return ESP_OK;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    ESP_LOGD(TAG, "Creating directory: %s", full_path);
    
    if (mkdir(full_path, 0755) != 0) {
        if (errno == EEXIST) {
            ESP_LOGD(TAG, "Directory already exists: %s", full_path);
            xSemaphoreGive(m_mutex);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "mkdir failed: %s", strerror(errno));
        xSemaphoreGive(m_mutex);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Directory created: %s", full_path);
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

esp_err_t StorageFlashFAT::list_directory(const char* path,
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
    
    if (!path || !callback) {
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    DIR* dir = opendir(full_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", strerror(errno));
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
        snprintf(entry_path, sizeof(entry_path), "%s/%s", full_path, entry->d_name);
        
        struct stat st;
        bool is_dir = false;
        uint64_t size = 0;
        uint32_t timestamp = 0;
        
        if (stat(entry_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size = st.st_size;
            timestamp = (uint32_t)st.st_mtime;
        }
        
        // Call callback
        callback(entry->d_name, size, is_dir, timestamp, user_data);
    }
    
    closedir(dir);
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

esp_err_t StorageFlashFAT::delete_directory_recursive(const char* path) {
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
    
    // Can't delete root
    if (is_root_path(path)) {
        ESP_LOGE(TAG, "Cannot delete root directory");
        xSemaphoreGive(m_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    resolve_path(path, full_path, sizeof(full_path));
    
    ESP_LOGD(TAG, "Deleting directory recursively: %s", full_path);
    
    esp_err_t ret = delete_directory_recursive_impl(full_path);
    
    xSemaphoreGive(m_mutex);
    return ret;
}

/* ============================================================================
 * Advanced Operations
 * ========================================================================= */

esp_err_t StorageFlashFAT::get_backend_info(StorageBackendInfo* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(info, 0, sizeof(StorageBackendInfo));
    
    // Basic info
    strncpy(info->mount_point, m_mount_point, sizeof(info->mount_point) - 1);
    info->type = static_cast<uint8_t>(get_type());
    strncpy(info->type_name, get_type_name(), sizeof(info->type_name) - 1);
    info->capabilities = get_capabilities();
    info->mounted = m_mounted;
    
    // Space info (if mounted)
    if (m_mounted) {
        get_space_info(&info->total_bytes, &info->free_bytes);
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Private Helper Methods
 * ========================================================================= */

esp_err_t StorageFlashFAT::init_wear_leveling() {
    // Find partition by label
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        m_partition_label
    );
    
    if (!partition) {
        ESP_LOGE(TAG, "Partition '%s' not found", m_partition_label);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Found partition '%s': offset=0x%lx size=%lu KB",
             m_partition_label, partition->address, partition->size / 1024);
    
    // Initialize wear leveling
    esp_err_t ret = wl_mount(partition, &m_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wear leveling init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Wear leveling initialized (handle=%d)", m_wl_handle);
    return ESP_OK;
}

void StorageFlashFAT::resolve_path(const char* relative_path, 
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

esp_err_t StorageFlashFAT::delete_directory_recursive_impl(const char* path) {
    // Note: Caller must hold mutex
    
    DIR* dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory for recursive delete: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    struct dirent* entry;
    esp_err_t ret = ESP_OK;
    
    while ((entry = readdir(dir)) != NULL && ret == ESP_OK) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build full path
        char entry_path[512];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", path, entry->d_name);
        
        // Check if directory
        struct stat st;
        if (stat(entry_path, &st) != 0) {
            ESP_LOGE(TAG, "stat failed during recursive delete: %s", strerror(errno));
            ret = ESP_FAIL;
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            // Recursively delete subdirectory
            ret = delete_directory_recursive_impl(entry_path);
        } else {
            // Delete file
            if (unlink(entry_path) != 0) {
                ESP_LOGE(TAG, "Failed to delete file %s: %s", entry_path, strerror(errno));
                ret = ESP_FAIL;
            }
        }
    }
    
    closedir(dir);
    
    // Delete the directory itself
    if (ret == ESP_OK) {
        if (rmdir(path) != 0) {
            ESP_LOGE(TAG, "Failed to delete directory %s: %s", path, strerror(errno));
            ret = ESP_FAIL;
        } else {
            ESP_LOGD(TAG, "Deleted directory: %s", path);
        }
    }
    
    return ret;
}

bool StorageFlashFAT::is_root_path(const char* path) const {
    if (!path) {
        return false;
    }
    
    return (path[0] == '\0' || (path[0] == '/' && path[1] == '\0'));
}
