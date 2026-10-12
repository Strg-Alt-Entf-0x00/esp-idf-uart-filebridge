/*
 * ESP-IDF UART Filebridge - SD/FAT Storage Backend Implementation
 *
 * Extracted from FilesystemManager v1.x with multi-backend support.
 */

#include "storage_sd_fat.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include <sys/stat.h>
#include <sys/unistd.h>
#include <cstring>
#include <dirent.h>

/* ESP32-P4 LDO power control */
#if defined(CONFIG_IDF_TARGET_ESP32P4) && defined(CONFIG_UART_FILEBRIDGE_P4_LDO_ENABLE)
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#define FILEBRIDGE_P4_LDO 1
#else
#define FILEBRIDGE_P4_LDO 0
#endif

static const char* TAG = "storage_sd_fat";

/* ============================================================================
 * Constructor / Destructor
 * ========================================================================= */

StorageSDFAT::StorageSDFAT(const char* mount_point)
    : m_mounted(false)
    , m_owns_mount(false)
    , m_sd_card(nullptr)
    , m_sd_pwr_ctrl(nullptr)
    , m_mutex(nullptr)
{
    if (!mount_point) {
        ESP_LOGE(TAG, "Mount point is NULL");
        return;
    }
    
    size_t len = strlen(mount_point);
    if (len >= sizeof(m_mount_point)) {
        ESP_LOGE(TAG, "Mount point too long: %zu >= %zu", len, sizeof(m_mount_point));
        return;
    }
    
    strncpy(m_mount_point, mount_point, sizeof(m_mount_point) - 1);
    m_mount_point[sizeof(m_mount_point) - 1] = '\0';
    
    // Create mutex for thread-safe operations
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }
}

StorageSDFAT::~StorageSDFAT() {
    if (m_mounted && m_owns_mount) {
        unmount();
    }
    
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

/* ============================================================================
 * Lifecycle Management
 * ========================================================================= */

esp_err_t StorageSDFAT::mount() {
    if (!m_mutex) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Already mounted
    if (m_mounted) {
        xSemaphoreGive(m_mutex);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Mounting SD card at %s", m_mount_point);
    
    // Configure mount options
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files              = CONFIG_UART_FILEBRIDGE_SD_MAX_FILES,
        .allocation_unit_size   = 16 * 1024,  // 16KB clusters for better performance
        .disk_status_check_enable = true,
        .use_one_fat            = false,      // Use both FAT copies for reliability
    };
    
    // Configure SDMMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40 MHz
    
#if FILEBRIDGE_P4_LDO
    // ESP32-P4: Initialize on-chip LDO for SD power
    esp_err_t ret = init_p4_ldo_power();
    if (ret != ESP_OK) {
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    // Attach LDO to host (cast void* to proper type)
    host.pwr_ctrl_handle = (sd_pwr_ctrl_handle_t)m_sd_pwr_ctrl;
#endif
    
    // Configure slot (pin configuration)
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    // ESP32-P4 (Waveshare ESP32-P4-WIFI6 board defaults)
    slot_config.clk   = (gpio_num_t)43;
    slot_config.cmd   = (gpio_num_t)44;
    slot_config.d0    = (gpio_num_t)39;
    slot_config.d1    = (gpio_num_t)40;
    slot_config.d2    = (gpio_num_t)41;
    slot_config.d3    = (gpio_num_t)42;
    slot_config.width = 4;  // 4-bit mode
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
#endif
    
    // Mount SD card (reuse ret variable)
    ret = esp_vfs_fat_sdmmc_mount(m_mount_point, &host,
                                  &slot_config, &mount_config,
                                  &m_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        
#if FILEBRIDGE_P4_LDO
        deinit_p4_ldo_power();
#endif
        
        xSemaphoreGive(m_mutex);
        return ret;
    }
    
    // Log card info
    if (m_sd_card) {
        uint32_t freq_khz = m_sd_card->max_freq_khz;
        uint64_t capacity_mb = ((uint64_t)m_sd_card->csd.capacity * 
                               m_sd_card->csd.sector_size) / (1024 * 1024);
        
        ESP_LOGI(TAG, "SD card mounted successfully");
        ESP_LOGI(TAG, "  Name: %s", m_sd_card->cid.name);
        ESP_LOGI(TAG, "  Frequency: %u kHz (%u MHz)", freq_khz, freq_khz / 1000);
        ESP_LOGI(TAG, "  Capacity: %llu MB", capacity_mb);
    }
    
    m_mounted = true;
    m_owns_mount = true;
    
    xSemaphoreGive(m_mutex);
    
    // Cleanup partial uploads from previous sessions
    cleanup_partial_uploads_in_dir(m_mount_point);
    
    return ESP_OK;
}

esp_err_t StorageSDFAT::unmount() {
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
    
    ESP_LOGI(TAG, "Unmounting SD card at %s", m_mount_point);
    
    // Unmount VFS
    esp_vfs_fat_sdcard_unmount(m_mount_point, m_sd_card);
    m_sd_card = nullptr;
    
#if FILEBRIDGE_P4_LDO
    deinit_p4_ldo_power();
#endif
    
    m_mounted = false;
    m_owns_mount = false;
    
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

bool StorageSDFAT::is_mounted() const {
    return m_mounted;
}

/* ============================================================================
 * Backend Information
 * ========================================================================= */

uint32_t StorageSDFAT::get_capabilities() const {
    // SD/FAT supports all operations except compression
    return CAP_READ | CAP_WRITE | CAP_DIRECTORIES | 
           CAP_RECURSIVE_DELETE | CAP_RENAME | CAP_COPY |
           CAP_HASH | CAP_FORMAT | CAP_TIMESTAMPS | 
           CAP_LARGE_FILES;  // exFAT supports >4GB files
}

const char* StorageSDFAT::get_type_name() const {
    return "SD/FAT";
}

const char* StorageSDFAT::get_mount_point() const {
    return m_mount_point;
}

StorageType StorageSDFAT::get_type() const {
    return StorageType::SD_FAT;
}

esp_err_t StorageSDFAT::get_backend_info(StorageBackendInfo* info) {
    if (!info) {
        ESP_LOGE(TAG, "Info pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(info, 0, sizeof(StorageBackendInfo));
    
    // Copy mount point
    strncpy(info->mount_point, m_mount_point, sizeof(info->mount_point) - 1);
    
    // Set type and capabilities
    info->type = static_cast<uint8_t>(get_type());
    info->capabilities = get_capabilities();
    info->mounted = m_mounted;
    strncpy(info->type_name, get_type_name(), sizeof(info->type_name) - 1);
    
    // Query space info if mounted
    if (m_mounted) {
        get_space_info(&info->total_bytes, &info->free_bytes);
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Space Information
 * ========================================================================= */

esp_err_t StorageSDFAT::get_space_info(uint64_t* total_bytes, uint64_t* free_bytes) {
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    FATFS* fs;
    DWORD fre_clust;
    
    // Query FAT filesystem
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "f_getfree failed: %d", res);
        return ESP_FAIL;
    }
    
    // Calculate total and free bytes
    uint64_t tot_sect = (fs->n_fatent - 2) * fs->csize;
    uint64_t fre_sect = fre_clust * fs->csize;
    
    if (total_bytes) {
        *total_bytes = tot_sect * fs->ssize;
    }
    
    if (free_bytes) {
        *free_bytes = fre_sect * fs->ssize;
    }
    
    return ESP_OK;
}

/* ============================================================================
 * File Operations
 * ========================================================================= */

esp_err_t StorageSDFAT::open_file(const char* path, const char* mode, FILE** out) {
    if (!path || !mode || !out) {
        ESP_LOGE(TAG, "Invalid arguments to open_file");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char full_path[512];
    ret = build_full_path(path, full_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    FILE* file = fopen(full_path, mode);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    *out = file;
    return ESP_OK;
}

esp_err_t StorageSDFAT::stat_file(const char* path, uint64_t* size,
                                   uint32_t* timestamp, bool* is_dir) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char full_path[512];
    ret = build_full_path(path, full_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (size) {
        *size = (uint64_t)st.st_size;
    }
    
    if (timestamp) {
        *timestamp = (uint32_t)st.st_mtime;
    }
    
    if (is_dir) {
        *is_dir = S_ISDIR(st.st_mode);
    }
    
    return ESP_OK;
}

esp_err_t StorageSDFAT::delete_file(const char* path) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Cannot delete root
    if (strlen(path) == 0 || strcmp(path, "/") == 0) {
        ESP_LOGE(TAG, "Cannot delete root directory");
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[512];
    ret = build_full_path(path, full_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // If directory, use recursive delete
    if (S_ISDIR(st.st_mode)) {
        ESP_LOGE(TAG, "Path is a directory, use delete_directory_recursive");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (unlink(full_path) != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s", full_path);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t StorageSDFAT::rename_file(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) {
        ESP_LOGE(TAG, "Invalid arguments to rename_file");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(old_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = validate_relative_path(new_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char old_full[512], new_full[512];
    ret = build_full_path(old_path, old_full);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = build_full_path(new_path, new_full);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Check if destination exists and remove it
    // (FAT rename behavior - overwrite destination)
    struct stat st;
    if (stat(new_full, &st) == 0) {
        unlink(new_full);
    }
    
    // CRITICAL: On some SD cards, rename immediately after write may fail
    // Allow SD card to finish physical writes before rename
    vTaskDelay(pdMS_TO_TICKS(50));
    
    if (rename(old_full, new_full) != 0) {
        ESP_LOGE(TAG, "Failed to rename: %s -> %s", old_full, new_full);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Directory Operations
 * ========================================================================= */

esp_err_t StorageSDFAT::create_directory(const char* path) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Cannot create root
    if (strlen(path) == 0 || strcmp(path, "/") == 0) {
        ESP_LOGE(TAG, "Cannot create root directory");
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[512];
    ret = build_full_path(path, full_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Check if already exists
    struct stat st;
    if (stat(full_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;  // Already exists as directory
        } else {
            ESP_LOGE(TAG, "Path exists as file: %s", full_path);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    if (mkdir(full_path, 0755) != 0) {
        ESP_LOGE(TAG, "Failed to create directory: %s", full_path);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t StorageSDFAT::list_directory(const char* path,
                                        void (*callback)(const char*, uint64_t, bool,
                                                        uint32_t, void*),
                                        void* user_data) {
    if (!path || !callback) {
        ESP_LOGE(TAG, "Invalid arguments to list_directory");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char dir_path[512];
    ret = build_full_path(path, dir_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    DIR* dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dir_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    char* full_path = (char*)malloc(1024);  // Increased buffer size to avoid truncation
    if (!full_path) {
        closedir(dir);
        ESP_LOGE(TAG, "Failed to allocate path buffer");
        return ESP_ERR_NO_MEM;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Build full path for stat
        snprintf(full_path, 1024, "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            callback(entry->d_name, (uint64_t)st.st_size,
                    S_ISDIR(st.st_mode), (uint32_t)st.st_mtime, user_data);
        }
    }
    
    free(full_path);
    closedir(dir);
    return ESP_OK;
}

esp_err_t StorageSDFAT::delete_directory_recursive(const char* path) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Cannot delete root
    if (strlen(path) == 0 || strcmp(path, "/") == 0) {
        ESP_LOGE(TAG, "Cannot delete root directory");
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[512];
    ret = build_full_path(path, full_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return delete_directory_recursive_impl(full_path);
}

/* ============================================================================
 * Advanced Operations
 * ========================================================================= */

esp_err_t StorageSDFAT::copy_file(const char* src_path, const char* dst_path) {
    if (!src_path || !dst_path) {
        ESP_LOGE(TAG, "Invalid arguments to copy_file");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(src_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = validate_relative_path(dst_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char src_full[512], dst_full[512];
    ret = build_full_path(src_path, src_full);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = build_full_path(dst_path, dst_full);
    if (ret != ESP_OK) {
        return ret;
    }
    
    FILE* src = fopen(src_full, "rb");
    if (!src) {
        ESP_LOGE(TAG, "Failed to open source: %s", src_full);
        return ESP_ERR_NOT_FOUND;
    }
    
    FILE* dst = fopen(dst_full, "wb");
    if (!dst) {
        fclose(src);
        ESP_LOGE(TAG, "Failed to open destination: %s", dst_full);
        return ESP_FAIL;
    }
    
    const size_t buf_size = 16 * 1024;  // 16KB buffer for faster SD I/O
    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (!buf) {
        fclose(src);
        fclose(dst);
        unlink(dst_full);
        ESP_LOGE(TAG, "Failed to allocate copy buffer");
        return ESP_ERR_NO_MEM;
    }
    
    size_t n;
    esp_err_t result = ESP_OK;
    
    while ((n = fread(buf, 1, buf_size, src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            ESP_LOGE(TAG, "Write failed during copy");
            result = ESP_FAIL;
            break;
        }
    }
    
    free(buf);
    fclose(src);
    fclose(dst);
    
    if (result != ESP_OK) {
        unlink(dst_full);
    }
    
    return result;
}

esp_err_t StorageSDFAT::hash_file(const char* path, uint32_t* hash) {
    if (!path || !hash) {
        ESP_LOGE(TAG, "Invalid arguments to hash_file");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = validate_relative_path(path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char full_path[512];
    ret = build_full_path(path, full_path);
    if (ret != ESP_OK) {
        return ret;
    }
    
    FILE* f = fopen(full_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for hashing: %s", full_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    const size_t buf_size = 16 * 1024;
    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "Failed to allocate hash buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // CRC32 calculation (IEEE 802.3 polynomial)
    uint32_t crc = 0xFFFFFFFF;
    size_t n;
    
    while ((n = fread(buf, 1, buf_size, f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            crc ^= buf[i];
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
            }
        }
    }
    
    free(buf);
    fclose(f);
    
    *hash = ~crc;
    return ESP_OK;
}

esp_err_t StorageSDFAT::format() {
    if (!m_sd_card) {
        ESP_LOGE(TAG, "No SD card available for format");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!m_mounted) {
        ESP_LOGE(TAG, "SD card must be mounted before format");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "Formatting SD card at %s...", m_mount_point);
    
    esp_err_t ret = esp_vfs_fat_sdcard_format(m_mount_point, m_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        
        // Attempt remount to recover
        unmount();
        mount();
    } else {
        ESP_LOGI(TAG, "SD card formatted successfully");
    }
    
    return ret;
}

/* ============================================================================
 * Private Helper Functions
 * ========================================================================= */

esp_err_t StorageSDFAT::build_full_path(const char* relative_path, char* full_path) const {
    if (!relative_path || !full_path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t mount_len = strlen(m_mount_point);
    size_t rel_len = strlen(relative_path);
    
    // Check for overflow (mount + relative + null)
    if (mount_len + rel_len + 1 >= 512) {
        ESP_LOGE(TAG, "Path too long: %s + %s", m_mount_point, relative_path);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Build path: mount_point + relative_path
    memcpy(full_path, m_mount_point, mount_len);
    memcpy(full_path + mount_len, relative_path, rel_len + 1);
    
    return ESP_OK;
}

esp_err_t StorageSDFAT::validate_relative_path(const char* path) const {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t len = strlen(path);
    if (len == 0) {
        ESP_LOGE(TAG, "Path is empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (len >= 256) {
        ESP_LOGE(TAG, "Path too long: %zu", len);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check for ".." traversal
    const char* segment = path;
    while ((segment = strstr(segment, "..")) != nullptr) {
        const char before = segment == path ? '/' : segment[-1];
        const char after = segment[2];
        
        if ((before == '/' || before == '\\') && 
            (after == '/' || after == '\\' || after == '\0')) {
            ESP_LOGE(TAG, "Path contains .. traversal: %s", path);
            return ESP_ERR_INVALID_ARG;
        }
        
        segment += 2;
    }
    
    // Check for double slashes
    if (strstr(path, "//")) {
        ESP_LOGE(TAG, "Path contains double slashes: %s", path);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

esp_err_t StorageSDFAT::cleanup_partial_uploads_in_dir(const char* dir_path) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return ESP_FAIL;
    }
    
    constexpr const char* suffix = ".esp-idf-uart-filebridge.part";
    esp_err_t result = ESP_OK;
    struct dirent* entry;
    
    char* full_path = (char*)malloc(512);
    if (!full_path) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }
    
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        const size_t name_len = strlen(entry->d_name);
        const size_t path_len = strlen(dir_path);
        
        if (path_len + 1 + name_len >= 512) {
            result = ESP_FAIL;
            continue;
        }
        
        memcpy(full_path, dir_path, path_len);
        full_path[path_len] = '/';
        memcpy(full_path + path_len + 1, entry->d_name, name_len + 1);
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            result = ESP_FAIL;
            continue;
        }
        
        // Recurse into subdirectories
        if (S_ISDIR(st.st_mode)) {
            if (cleanup_partial_uploads_in_dir(full_path) != ESP_OK) {
                result = ESP_FAIL;
            }
            continue;
        }
        
        // Delete partial upload files
        if (name_len >= strlen(suffix) &&
            strcmp(entry->d_name + name_len - strlen(suffix), suffix) == 0) {
            if (unlink(full_path) != 0) {
                ESP_LOGW(TAG, "Failed to delete partial upload: %s", full_path);
                result = ESP_FAIL;
            } else {
                ESP_LOGI(TAG, "Cleaned up partial upload: %s", full_path);
            }
        }
    }
    
    free(full_path);
    closedir(dir);
    return result;
}

esp_err_t StorageSDFAT::delete_directory_recursive_impl(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    char* full_path = (char*)malloc(512);
    if (!full_path) {
        closedir(dir);
        ESP_LOGE(TAG, "Failed to allocate path buffer");
        return ESP_ERR_NO_MEM;
    }
    
    struct dirent* entry;
    esp_err_t result = ESP_OK;
    
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        int len = snprintf(full_path, 512, "%s/%s", path, entry->d_name);
        if (len >= 512) {
            result = ESP_ERR_INVALID_SIZE;
            continue;
        }
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            esp_err_t ret = S_ISDIR(st.st_mode)
                ? delete_directory_recursive_impl(full_path)
                : (unlink(full_path) == 0 ? ESP_OK : ESP_FAIL);
            
            if (ret != ESP_OK) {
                result = ret;
            }
        }
    }
    
    free(full_path);
    closedir(dir);
    
    // Delete the directory itself
    if (rmdir(path) != 0) {
        ESP_LOGE(TAG, "Failed to delete directory: %s", path);
        return ESP_FAIL;
    }
    
    return result;
}

#if FILEBRIDGE_P4_LDO
esp_err_t StorageSDFAT::init_p4_ldo_power() {
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = CONFIG_UART_FILEBRIDGE_P4_LDO_CHAN,
    };
    
    sd_pwr_ctrl_handle_t pwr_ctrl = nullptr;
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP32-P4 SD LDO init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    m_sd_pwr_ctrl = pwr_ctrl;
    ESP_LOGI(TAG, "ESP32-P4 SD LDO initialized");
    return ESP_OK;
}

void StorageSDFAT::deinit_p4_ldo_power() {
    if (m_sd_pwr_ctrl) {
        sd_pwr_ctrl_del_on_chip_ldo((sd_pwr_ctrl_handle_t)m_sd_pwr_ctrl);
        m_sd_pwr_ctrl = nullptr;
        ESP_LOGI(TAG, "ESP32-P4 SD LDO powered off");
    }
}
#endif
