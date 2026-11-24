/*
 * ESP-IDF UART Filebridge - Storage Registry Implementation
 */

#include "storage_registry.h"
#include <cstring>
#include "esp_log.h"

static const char* TAG = "storage_registry";

// Singleton instance
StorageRegistry* StorageRegistry::s_instance = nullptr;

StorageRegistry::StorageRegistry()
    : m_backend_count(0)
    , m_mutex(nullptr)
{
    // Initialize mutex for thread-safety
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create registry mutex");
        // Critical failure - cannot proceed without mutex
        abort();
    }
    
    // Initialize backend array to known state
    memset(m_backends, 0, sizeof(m_backends));
    
    ESP_LOGI(TAG, "Storage registry initialized (max %d backends)", MAX_STORAGE_BACKENDS);
}

StorageRegistry::~StorageRegistry() {
    // Clean up owned backends
    reset();
    
    // Delete mutex
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
    
    ESP_LOGI(TAG, "Storage registry destroyed");
}

StorageRegistry& StorageRegistry::instance() {
    // Thread-safe lazy initialization (C++11 guarantees this)
    static StorageRegistry instance;
    return instance;
}

esp_err_t StorageRegistry::register_backend(StorageBackend* backend, bool owns) {
    // Validate input
    if (!backend) {
        ESP_LOGE(TAG, "Cannot register NULL backend");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* mount_point = backend->get_mount_point();
    if (!mount_point) {
        ESP_LOGE(TAG, "Backend has NULL mount point");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate mount point format
    if (!validate_mount_point(mount_point)) {
        ESP_LOGE(TAG, "Invalid mount point format: %s", mount_point);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Acquire mutex for thread-safety
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire registry mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Check if registry is full
    if (m_backend_count >= MAX_STORAGE_BACKENDS) {
        ESP_LOGE(TAG, "Registry full (max %d backends)", MAX_STORAGE_BACKENDS);
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    
    // Check for duplicate or overlapping mount points
    for (size_t i = 0; i < m_backend_count; i++) {
        if (strcmp(m_backends[i].mount_point, mount_point) == 0) {
            ESP_LOGE(TAG, "Backend already registered at %s", mount_point);
            ret = ESP_ERR_INVALID_STATE;
            goto cleanup;
        }
        
        if (check_mount_overlap(m_backends[i].mount_point, mount_point)) {
            ESP_LOGE(TAG, "Mount point %s overlaps with existing %s",
                     mount_point, m_backends[i].mount_point);
            ret = ESP_ERR_INVALID_STATE;
            goto cleanup;
        }
    }
    
    // Register backend
    {
        BackendEntry& entry = m_backends[m_backend_count];
        entry.backend = backend;
        strncpy(entry.mount_point, mount_point, sizeof(entry.mount_point) - 1);
        entry.mount_point[sizeof(entry.mount_point) - 1] = '\0';
        entry.owns = owns;
        entry.mount_point_len = strlen(entry.mount_point);
        
        m_backend_count++;
        
        ESP_LOGI(TAG, "Registered backend: %s at %s (owns=%d, type=%s)",
                 backend->get_type_name(), mount_point, owns,
                 backend->is_mounted() ? "mounted" : "unmounted");
    }

cleanup:
    xSemaphoreGive(m_mutex);
    return ret;
}

esp_err_t StorageRegistry::unregister_backend(const char* mount_point) {
    if (!mount_point) {
        ESP_LOGE(TAG, "Cannot unregister NULL mount point");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire registry mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Find backend
    size_t index = find_backend_index(mount_point);
    if (index >= m_backend_count) {
        ESP_LOGW(TAG, "Backend not found: %s", mount_point);
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }
    
    // Delete backend if owned
    if (m_backends[index].owns && m_backends[index].backend) {
        ESP_LOGI(TAG, "Deleting owned backend: %s", mount_point);
        delete m_backends[index].backend;
    }
    
    // Remove from array by shifting remaining entries
    for (size_t i = index; i < m_backend_count - 1; i++) {
        m_backends[i] = m_backends[i + 1];
    }
    
    // Clear last entry
    memset(&m_backends[m_backend_count - 1], 0, sizeof(BackendEntry));
    m_backend_count--;
    
    ESP_LOGI(TAG, "Unregistered backend: %s (%d backends remaining)",
             mount_point, (int)m_backend_count);

cleanup:
    xSemaphoreGive(m_mutex);
    return ret;
}

StorageBackend* StorageRegistry::resolve_path(const char* path, const char** relative_path) {
    if (!path) {
        ESP_LOGE(TAG, "Cannot resolve NULL path");
        return nullptr;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire registry mutex");
        return nullptr;
    }
    
    StorageBackend* result = nullptr;
    size_t longest_match_len = 0;
    const char* best_relative_path = nullptr;
    
    // Find backend with longest matching prefix
    for (size_t i = 0; i < m_backend_count; i++) {
        const BackendEntry& entry = m_backends[i];
        const size_t mount_len = entry.mount_point_len;
        
        // Check if path starts with mount point
        if (strncmp(path, entry.mount_point, mount_len) == 0) {
            // Ensure it's a complete path component match:
            // - Exact match: "/sd" matches "/sd"
            // - Prefix match: "/sd" matches "/sd/file.txt" (next char is '/')
            const char next_char = path[mount_len];
            if (next_char == '\0' || next_char == '/') {
                // This backend matches - check if it's the longest match
                if (mount_len > longest_match_len) {
                    longest_match_len = mount_len;
                    result = entry.backend;
                    
                    // Calculate relative path
                    if (next_char == '/') {
                        best_relative_path = path + mount_len;  // Points to "/file.txt"
                    } else {
                        // Exact match - relative path is root "/"
                        best_relative_path = "/";
                    }
                }
            }
        }
    }
    
    // Return relative path if requested
    if (relative_path) {
        *relative_path = best_relative_path;
    }
    
    if (!result) {
        ESP_LOGD(TAG, "No backend found for path: %s", path);
    } else {
        ESP_LOGV(TAG, "Resolved %s → backend %s, relative %s",
                 path, result->get_mount_point(),
                 best_relative_path ? best_relative_path : "(null)");
    }
    
    xSemaphoreGive(m_mutex);
    return result;
}

size_t StorageRegistry::get_backend_count() const {
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire registry mutex");
        return 0;
    }
    
    size_t count = m_backend_count;
    
    xSemaphoreGive(m_mutex);
    return count;
}

StorageBackend* StorageRegistry::get_backend(size_t index) {
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire registry mutex");
        return nullptr;
    }
    
    StorageBackend* backend = nullptr;
    
    if (index < m_backend_count) {
        backend = m_backends[index].backend;
    }
    
    xSemaphoreGive(m_mutex);
    return backend;
}

StorageBackend* StorageRegistry::get_backend_by_mount(const char* mount_point) {
    if (!mount_point) {
        return nullptr;
    }
    
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire registry mutex");
        return nullptr;
    }
    
    StorageBackend* backend = nullptr;
    size_t index = find_backend_index(mount_point);
    
    if (index < m_backend_count) {
        backend = m_backends[index].backend;
    }
    
    xSemaphoreGive(m_mutex);
    return backend;
}

esp_err_t StorageRegistry::validate_path(const char* path) {
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Must start with '/'
    if (path[0] != '/') {
        ESP_LOGD(TAG, "Path must start with '/': %s", path);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check for ".." traversal and empty components
    const char* p = path;
    while (*p) {
        if (*p == '/') {
            // Check for "//"
            if (*(p + 1) == '/') {
                ESP_LOGD(TAG, "Path contains empty component: %s", path);
                return ESP_ERR_INVALID_ARG;
            }
            
            // Check for "/.."
            if (*(p + 1) == '.' && *(p + 2) == '.') {
                const char next = *(p + 3);
                if (next == '/' || next == '\0') {
                    ESP_LOGD(TAG, "Path contains '..' traversal: %s", path);
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
        p++;
    }
    
    // Check length (arbitrary limit to prevent buffer overflows downstream)
    size_t len = strlen(path);
    if (len >= 256) {
        ESP_LOGD(TAG, "Path too long (%d chars): %s", (int)len, path);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if backend exists for this path
    const char* relative_path;
    StorageBackend* backend = resolve_path(path, &relative_path);
    if (!backend) {
        ESP_LOGD(TAG, "No backend for path: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

esp_err_t StorageRegistry::reset() {
    if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire registry mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "Resetting registry (%d backends)", (int)m_backend_count);
    
    // Delete owned backends
    for (size_t i = 0; i < m_backend_count; i++) {
        if (m_backends[i].owns && m_backends[i].backend) {
            ESP_LOGI(TAG, "Deleting owned backend: %s", m_backends[i].mount_point);
            delete m_backends[i].backend;
        }
    }
    
    // Clear array
    memset(m_backends, 0, sizeof(m_backends));
    m_backend_count = 0;
    
    ESP_LOGI(TAG, "Registry reset complete");
    
    xSemaphoreGive(m_mutex);
    return ESP_OK;
}

bool StorageRegistry::check_mount_overlap(const char* mount1, const char* mount2) const {
    size_t len1 = strlen(mount1);
    size_t len2 = strlen(mount2);
    
    // Check if mount1 is prefix of mount2
    if (len1 < len2) {
        if (strncmp(mount1, mount2, len1) == 0) {
            // mount1 is prefix - check if it's a complete component
            if (mount2[len1] == '/') {
                return true;  // Overlap: "/sd" is prefix of "/sd/models"
            }
        }
    }
    
    // Check if mount2 is prefix of mount1
    if (len2 < len1) {
        if (strncmp(mount2, mount1, len2) == 0) {
            // mount2 is prefix - check if it's a complete component
            if (mount1[len2] == '/') {
                return true;  // Overlap: "/sd/models" has prefix "/sd"
            }
        }
    }
    
    return false;  // No overlap
}

bool StorageRegistry::validate_mount_point(const char* mount_point) const {
    if (!mount_point || mount_point[0] == '\0') {
        return false;  // NULL or empty
    }
    
    // Must start with '/'
    if (mount_point[0] != '/') {
        return false;
    }
    
    // Special case: root "/" is valid
    if (strcmp(mount_point, "/") == 0) {
        return true;
    }
    
    // Must not end with '/' (except root)
    size_t len = strlen(mount_point);
    if (mount_point[len - 1] == '/') {
        return false;
    }
    
    // Check for ".." or empty components
    const char* p = mount_point;
    while (*p) {
        if (*p == '/') {
            // Check for "//"
            if (*(p + 1) == '/') {
                return false;
            }
            
            // Check for "/.."
            if (*(p + 1) == '.' && *(p + 2) == '.') {
                const char next = *(p + 3);
                if (next == '/' || next == '\0') {
                    return false;
                }
            }
        }
        p++;
    }
    
    // Check length
    if (len >= 64) {
        return false;  // Too long for mount point
    }
    
    return true;
}

size_t StorageRegistry::find_backend_index(const char* mount_point) const {
    // Linear search (acceptable for small number of backends)
    for (size_t i = 0; i < m_backend_count; i++) {
        if (strcmp(m_backends[i].mount_point, mount_point) == 0) {
            return i;
        }
    }
    return m_backend_count;  // Not found
}

