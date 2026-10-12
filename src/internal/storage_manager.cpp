/*
 * ESP-IDF UART Filebridge - Storage Manager Implementation
 * 
 * Backend-neutral routing layer. All operations delegate to backends via registry.
 */

#include "storage_manager.h"
#include "storage_registry.h"
#include "../backends/storage_sd_fat.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "storage_manager";

/* ============================================================================
 * Constructor / Destructor
 * ========================================================================= */

StorageManager::StorageManager()
    : m_initialized(false)
    , m_default_backend(nullptr)
    , m_owns_default_backend(false)
    , m_registry(StorageRegistry::instance())
{
}

StorageManager::~StorageManager() {
    if (m_owns_default_backend && m_default_backend) {
        delete m_default_backend;
        m_default_backend = nullptr;
    }
}

/* ============================================================================
 * Initialization
 * ========================================================================= */

esp_err_t StorageManager::init(const char* mount_point, bool do_mount) {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    if (!mount_point) {
        ESP_LOGE(TAG, "Mount point is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(mount_point) >= 64) {
        ESP_LOGE(TAG, "Mount point too long");
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "Initializing with default SD backend at %s", mount_point);
    
    // Create default SD backend (v1.x compatibility)
    StorageSDFAT* sd_backend = new StorageSDFAT(mount_point);
    if (!sd_backend) {
        ESP_LOGE(TAG, "Failed to create SD backend");
        return ESP_ERR_NO_MEM;
    }
    
    // Register with registry (registry owns backend)
    esp_err_t ret = m_registry.register_backend(sd_backend, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register SD backend: %s", esp_err_to_name(ret));
        delete sd_backend;
        return ret;
    }
    
    m_default_backend = sd_backend;
    m_owns_default_backend = false;  // Registry owns it now
    m_initialized = true;
    
    // Mount if requested
    if (do_mount) {
        ret = sd_backend->mount();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SD mount failed: %s - file operations unavailable",
                    esp_err_to_name(ret));
            // Non-fatal: v1.x behavior - system continues, transfers will fail gracefully
        }
    } else {
        ESP_LOGI(TAG, "External SD mount assumed at %s", mount_point);
    }
    
    return ESP_OK;
}

esp_err_t StorageManager::init_multi_backend() {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing multi-backend mode (no default backends)");
    
    m_initialized = true;
    m_default_backend = nullptr;
    m_owns_default_backend = false;
    
    return ESP_OK;
}

/* ============================================================================
 * Backend Management
 * ========================================================================= */

esp_err_t StorageManager::register_backend(StorageBackend* backend, bool owns) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return m_registry.register_backend(backend, owns);
}

esp_err_t StorageManager::unregister_backend(const char* mount_point) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return m_registry.unregister_backend(mount_point);
}

size_t StorageManager::get_backend_count() const {
    return m_registry.get_backend_count();
}

StorageBackend* StorageManager::get_backend(size_t index) {
    return m_registry.get_backend(index);
}

StorageBackend* StorageManager::get_backend_by_mount(const char* mount_point) {
    return m_registry.get_backend_by_mount(mount_point);
}

/* ============================================================================
 * Space Information
 * ========================================================================= */

esp_err_t StorageManager::get_space_info(const char* path, 
                                         uint64_t* total_bytes,
                                         uint64_t* free_bytes) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve path to backend
    const char* relative_path;
    StorageBackend* backend = m_registry.resolve_path(path, &relative_path);
    
    if (!backend) {
        ESP_LOGE(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delegate to backend
    return backend->get_space_info(total_bytes, free_bytes);
}

/* ============================================================================
 * File Operations
 * ========================================================================= */

esp_err_t StorageManager::list_directory(const char* path,
                                         void (*callback)(const char*, uint64_t, bool,
                                                         uint32_t, void*),
                                         void* user_data) {
    if (!path || !callback) {
        ESP_LOGE(TAG, "Invalid arguments to list_directory");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve path to backend
    const char* relative_path;
    StorageBackend* backend = m_registry.resolve_path(path, &relative_path);
    
    if (!backend) {
        ESP_LOGE(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delegate to backend
    return backend->list_directory(relative_path, callback, user_data);
}

esp_err_t StorageManager::stat_file(const char* path, uint64_t* size,
                                    uint32_t* timestamp, bool* is_dir) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve path to backend
    const char* relative_path;
    StorageBackend* backend = m_registry.resolve_path(path, &relative_path);
    
    if (!backend) {
        ESP_LOGE(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delegate to backend
    return backend->stat_file(relative_path, size, timestamp, is_dir);
}

esp_err_t StorageManager::delete_file(const char* path) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve path to backend
    const char* relative_path;
    StorageBackend* backend = m_registry.resolve_path(path, &relative_path);
    
    if (!backend) {
        ESP_LOGE(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delegate to backend
    return backend->delete_file(relative_path);
}

esp_err_t StorageManager::rename_file(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) {
        ESP_LOGE(TAG, "Invalid arguments to rename_file");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve both paths
    const char* old_relative;
    StorageBackend* old_backend = m_registry.resolve_path(old_path, &old_relative);
    
    const char* new_relative;
    StorageBackend* new_backend = m_registry.resolve_path(new_path, &new_relative);
    
    if (!old_backend || !new_backend) {
        ESP_LOGE(TAG, "No backend for paths: %s -> %s", old_path, new_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Both paths must be on same backend
    if (old_backend != new_backend) {
        ESP_LOGE(TAG, "Cross-backend rename not supported: %s -> %s",
                old_path, new_path);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Delegate to backend
    return old_backend->rename_file(old_relative, new_relative);
}

esp_err_t StorageManager::create_directory(const char* path) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve path to backend
    const char* relative_path;
    StorageBackend* backend = m_registry.resolve_path(path, &relative_path);
    
    if (!backend) {
        ESP_LOGE(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delegate to backend
    return backend->create_directory(relative_path);
}

esp_err_t StorageManager::copy_file(const char* src_path, const char* dst_path) {
    if (!src_path || !dst_path) {
        ESP_LOGE(TAG, "Invalid arguments to copy_file");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve both paths
    const char* src_relative;
    StorageBackend* src_backend = m_registry.resolve_path(src_path, &src_relative);
    
    const char* dst_relative;
    StorageBackend* dst_backend = m_registry.resolve_path(dst_path, &dst_relative);
    
    if (!src_backend || !dst_backend) {
        ESP_LOGE(TAG, "No backend for paths: %s -> %s", src_path, dst_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Both paths must be on same backend
    if (src_backend != dst_backend) {
        ESP_LOGE(TAG, "Cross-backend copy not supported (higher layer responsibility): %s -> %s",
                src_path, dst_path);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Delegate to backend
    return src_backend->copy_file(src_relative, dst_relative);
}

esp_err_t StorageManager::hash_file(const char* path, uint32_t* hash) {
    if (!path || !hash) {
        ESP_LOGE(TAG, "Invalid arguments to hash_file");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve path to backend
    const char* relative_path;
    StorageBackend* backend = m_registry.resolve_path(path, &relative_path);
    
    if (!backend) {
        ESP_LOGE(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delegate to backend
    return backend->hash_file(relative_path, hash);
}

esp_err_t StorageManager::delete_directory_recursive(const char* path) {
    if (!path) {
        ESP_LOGE(TAG, "Path is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Resolve path to backend
    const char* relative_path;
    StorageBackend* backend = m_registry.resolve_path(path, &relative_path);
    
    if (!backend) {
        ESP_LOGE(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Delegate to backend
    return backend->delete_directory_recursive(relative_path);
}

esp_err_t StorageManager::validate_path(const char* path) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Delegate to registry
    return m_registry.validate_path(path);
}
