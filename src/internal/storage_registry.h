/*
 * ESP-IDF UART Filebridge - Storage Registry
 * 
 * Singleton registry for managing multiple storage backends and routing paths
 * to appropriate backends based on mount point matching.
 * 
 * The registry maintains a list of registered backends and provides thread-safe
 * access for path resolution, backend queries, and registration/unregistration.
 * 
 * Design Principles:
 * - Singleton: One global registry per system
 * - Thread-safe: All operations protected by mutex
 * - Longest-prefix matching: Routes paths to backend with longest matching prefix
 * - Validation: Rejects overlapping mount points
 * - Zero-copy: Path operations don't allocate memory
 * 
 * Version: 2.0.0
 * Date: 2026-10-12
 */

#pragma once

#include "storage_backend.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"

/**
 * Maximum number of backends that can be registered simultaneously.
 * 
 * Typical usage:
 * - 1-2 backends: SD + Flash
 * - 3-4 backends: SD + Flash + LittleFS + SPIFFS
 * - 8 allows for multiple SD cards or custom backends
 */
static constexpr size_t MAX_STORAGE_BACKENDS = 8;

/**
 * Storage Registry - Singleton manager for all storage backends.
 * 
 * The registry maintains a list of registered backends and routes file operations
 * to the appropriate backend based on path prefixes. For example:
 * - "/sd/file.txt" → SD backend
 * - "/flash/config.json" → Flash backend
 * 
 * Thread Safety:
 * All public methods are thread-safe and can be called from any FreeRTOS task.
 * The registry uses a mutex to protect internal state.
 * 
 * Usage Example:
 *   // Get singleton instance
 *   StorageRegistry& registry = StorageRegistry::instance();
 *   
 *   // Register backends
 *   StorageSDFAT sd_backend("/sd");
 *   registry.register_backend(&sd_backend, false);
 *   
 *   StorageFlashFAT flash_backend("/flash", "storage");
 *   registry.register_backend(&flash_backend, false);
 *   
 *   // Resolve paths
 *   const char* relative_path;
 *   StorageBackend* backend = registry.resolve_path("/sd/file.txt", &relative_path);
 *   // backend = &sd_backend, relative_path = "/file.txt"
 *   
 *   // Query backends
 *   for (size_t i = 0; i < registry.get_backend_count(); i++) {
 *       StorageBackend* b = registry.get_backend(i);
 *       ESP_LOGI(TAG, "Backend: %s", b->get_mount_point());
 *   }
 */
class StorageRegistry {
public:
    /**
     * Get singleton instance.
     * 
     * Thread-safe lazy initialization. First call creates the instance,
     * subsequent calls return the same instance.
     * 
     * @return Reference to singleton StorageRegistry
     * 
     * @note Thread-safe. Can be called from any task.
     * @note Instance lifetime is application lifetime (never destroyed).
     */
    static StorageRegistry& instance();
    
    // Disable copy and move
    StorageRegistry(const StorageRegistry&) = delete;
    StorageRegistry& operator=(const StorageRegistry&) = delete;
    StorageRegistry(StorageRegistry&&) = delete;
    StorageRegistry& operator=(StorageRegistry&&) = delete;
    
    /**
     * Register a storage backend.
     * 
     * Adds backend to the registry. The mount point must be unique and must not
     * overlap with existing mount points (e.g., "/sd" and "/sd/models" are not
     * allowed simultaneously).
     * 
     * @param backend  Pointer to backend instance (must remain valid)
     * @param owns     If true, registry will delete backend on unregister/deinit
     * @return ESP_OK              Backend registered successfully
     *         ESP_ERR_INVALID_ARG  backend is NULL or mount point invalid
     *         ESP_ERR_INVALID_STATE Mount point overlaps with existing backend
     *         ESP_ERR_NO_MEM       Registry is full (MAX_STORAGE_BACKENDS reached)
     * 
     * @note Thread-safe. Blocks while holding registry mutex.
     * @note Backend pointer must remain valid until unregistered.
     * @note If owns=true, backend will be deleted via delete operator.
     * @warning Mount points must not overlap (validated during registration).
     * 
     * Example:
     *   StorageSDFAT* sd = new StorageSDFAT("/sd");
     *   registry.register_backend(sd, true);  // Registry will delete sd
     *   
     *   StorageFlashFAT flash("/flash", "storage");
     *   registry.register_backend(&flash, false);  // User manages lifetime
     */
    esp_err_t register_backend(StorageBackend* backend, bool owns);
    
    /**
     * Unregister a storage backend.
     * 
     * Removes backend from registry. If backend was registered with owns=true,
     * it will be deleted. Active operations on the backend must complete before
     * calling this function.
     * 
     * @param mount_point  Mount point of backend to unregister (e.g., "/sd")
     * @return ESP_OK              Backend unregistered successfully
     *         ESP_ERR_INVALID_ARG  mount_point is NULL
     *         ESP_ERR_NOT_FOUND    No backend with this mount point
     *         ESP_ERR_INVALID_STATE Backend has active operations
     * 
     * @note Thread-safe. Blocks while holding registry mutex.
     * @note Does not force-unmount backend (unmount first if needed).
     * @warning Ensure no active operations before unregistering!
     */
    esp_err_t unregister_backend(const char* mount_point);
    
    /**
     * Resolve path to backend and relative path.
     * 
     * Finds the backend with the longest matching mount point prefix and
     * returns the path relative to that mount point.
     * 
     * @param path             Absolute VFS path (e.g., "/sd/file.txt")
     * @param[out] relative_path  Pointer to relative path within backend (may be NULL)
     * @return Pointer to matching backend, or NULL if no match
     * 
     * @note Thread-safe. Non-blocking (read lock only).
     * @note relative_path output is optional (may be NULL).
     * @note Returned relative_path points into input path string (no allocation).
     * @note NULL return means no backend matches path prefix.
     * 
     * Example:
     *   const char* rel_path;
     *   StorageBackend* backend = registry.resolve_path("/sd/models/model.bin", &rel_path);
     *   // backend points to SD backend
     *   // rel_path points to "/models/model.bin" within input string
     */
    StorageBackend* resolve_path(const char* path, const char** relative_path);
    
    /**
     * Get number of registered backends.
     * 
     * @return Number of currently registered backends (0 to MAX_STORAGE_BACKENDS)
     * 
     * @note Thread-safe. Non-blocking.
     */
    size_t get_backend_count() const;
    
    /**
     * Get backend by index.
     * 
     * Returns backend at specified index. Useful for iterating all backends.
     * 
     * @param index  Backend index (0 to get_backend_count()-1)
     * @return Pointer to backend, or NULL if index out of range
     * 
     * @note Thread-safe. Non-blocking.
     * @note Index may change if backends are registered/unregistered.
     * @note Always check return value for NULL.
     */
    StorageBackend* get_backend(size_t index);
    
    /**
     * Get backend by mount point.
     * 
     * Finds backend with exact mount point match.
     * 
     * @param mount_point  Mount point to search for (e.g., "/sd")
     * @return Pointer to backend, or NULL if not found
     * 
     * @note Thread-safe. Non-blocking.
     * @note Must be exact match (no prefix matching).
     */
    StorageBackend* get_backend_by_mount(const char* mount_point);
    
    /**
     * Validate path format and check for backend availability.
     * 
     * Performs comprehensive path validation:
     * - Must start with '/'
     * - No ".." traversal
     * - No empty components ("//")
     * - Length within limits
     * - Corresponds to registered backend
     * 
     * @param path  Path to validate
     * @return ESP_OK              Path is valid and backend exists
     *         ESP_ERR_INVALID_ARG  path is NULL or malformed
     *         ESP_ERR_NOT_FOUND    No backend for this path
     * 
     * @note Thread-safe. Non-blocking.
     * @note Does not check if file exists, only path validity.
     */
    esp_err_t validate_path(const char* path);
    
    /**
     * Reset registry to empty state.
     * 
     * Unregisters all backends. Backends registered with owns=true are deleted.
     * Used for testing and deinitialization.
     * 
     * @return ESP_OK always
     * 
     * @note Thread-safe. Blocks while holding registry mutex.
     * @warning Destructive operation! Ensure no active operations.
     * @note For testing only - normal applications should not call this.
     */
    esp_err_t reset();
    
    /**
     * Destructor - cleans up owned backends.
     * 
     * Called automatically at application exit. Deletes all backends registered
     * with owns=true.
     * 
     * @note Private destructor (singleton cannot be destroyed manually).
     */
    ~StorageRegistry();

private:
    /**
     * Private constructor - use instance() to get singleton.
     */
    StorageRegistry();
    
    /**
     * Backend registry entry.
     * 
     * Internal structure tracking registered backends and their metadata.
     */
    struct BackendEntry {
        StorageBackend* backend;        /**< Pointer to backend instance */
        char mount_point[64];           /**< Copy of mount point for fast lookup */
        bool owns;                      /**< If true, registry owns backend lifetime */
        size_t mount_point_len;         /**< Cached strlen(mount_point) for fast prefix matching */
    };
    
    /**
     * Check if two mount points overlap.
     * 
     * Returns true if one mount point is a prefix of the other.
     * Used to prevent registration of overlapping mounts.
     * 
     * @param mount1  First mount point
     * @param mount2  Second mount point
     * @return true if mount points overlap, false otherwise
     */
    bool check_mount_overlap(const char* mount1, const char* mount2) const;
    
    /**
     * Validate mount point format.
     * 
     * Ensures mount point:
     * - Starts with '/'
     * - No trailing slash (except root "/")
     * - No ".." or empty components
     * - Within length limits
     * 
     * @param mount_point  Mount point to validate
     * @return true if valid, false otherwise
     */
    bool validate_mount_point(const char* mount_point) const;
    
    /**
     * Find backend entry by mount point.
     * 
     * Linear search through registered backends. Used internally by
     * get_backend_by_mount and unregister_backend.
     * 
     * @param mount_point  Mount point to search for
     * @return Index of backend, or m_backend_count if not found
     * 
     * @note Not thread-safe - caller must hold mutex.
     */
    size_t find_backend_index(const char* mount_point) const;
    
    // Registry state
    BackendEntry m_backends[MAX_STORAGE_BACKENDS];  /**< Array of registered backends */
    size_t m_backend_count;                         /**< Number of registered backends */
    
    // Thread synchronization
    mutable SemaphoreHandle_t m_mutex;              /**< Mutex protecting registry state */
    
    // Singleton instance
    static StorageRegistry* s_instance;             /**< Singleton instance pointer */
};

