/*
 * ESP-IDF UART Filebridge - Storage Manager
 * 
 * Backend-neutral storage manager that routes file operations to appropriate
 * backends via StorageRegistry. Maintains backward compatibility with v1.x API
 * while enabling multi-backend support.
 * 
 * Design Principles:
 * - Routing layer: Delegates all operations to backends via registry
 * - Backward compatible: v1.x init() creates default SD backend
 * - Zero SD knowledge: No SD-specific code (all in StorageSDFAT)
 * - Thread-safe: All operations thread-safe via registry mutex
 * 
 * Version: 2.0.0
 * Date: 2026-10-12
 */

#pragma once

#include "storage_registry.h"
#include "storage_backend.h"
#include "esp_err.h"
#include <cstdint>
#include <cstdio>

/**
 * Storage Manager - Backend-neutral storage operations.
 * 
 * Routes file operations to appropriate backends based on path prefixes.
 * Provides both v1.x compatibility API and v2.0 multi-backend API.
 * 
 * Usage (v1.x compatibility):
 *   StorageManager mgr;
 *   mgr.init("/sd", true);  // Creates default SD backend
 *   mgr.delete_file("/sd/file.txt");  // Routes to SD backend
 * 
 * Usage (v2.0 multi-backend):
 *   StorageManager mgr;
 *   mgr.init_multi_backend();  // No automatic backends
 *   
 *   StorageSDFAT sd("/sd");
 *   mgr.register_backend(&sd, false);
 *   
 *   StorageFlashFAT flash("/flash", "storage");
 *   mgr.register_backend(&flash, false);
 *   
 *   mgr.delete_file("/sd/file.txt");     // Routes to SD
 *   mgr.delete_file("/flash/config.json");  // Routes to Flash
 * 
 * Thread Safety:
 * All operations are thread-safe via StorageRegistry mutex.
 */
class StorageManager {
public:
    /**
     * Constructor - initializes manager.
     */
    StorageManager();
    
    /**
     * Destructor - cleans up default backend if owned.
     */
    ~StorageManager();
    
    // Disable copy and move
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;
    StorageManager(StorageManager&&) = delete;
    StorageManager& operator=(StorageManager&&) = delete;
    
    /* ========================================================================
     * Initialization (v1.x Backward Compatibility)
     * ====================================================================== */
    
    /**
     * Initialize storage manager with default SD backend (v1.x compatibility).
     * 
     * Creates a StorageSDFAT backend and registers it with the registry.
     * If do_mount is true, mounts the SD card immediately.
     * 
     * @param mount_point  SD card mount point (default from Kconfig)
     * @param do_mount     If true, mount SD card now; if false, assume external mount
     * @return ESP_OK              Initialization successful
     *         ESP_ERR_INVALID_STATE Already initialized
     *         ESP_ERR_INVALID_SIZE   Mount point too long
     *         ESP_FAIL             SD mount failed (non-fatal if do_mount=false)
     * 
     * @note Thread-safe. Can only be called once.
     * @note Creates default SD backend owned by manager (deleted on deinit).
     * @note v1.x behavior: Mount failure is non-fatal, system continues.
     */
    esp_err_t init(const char* mount_point = "/sd", bool do_mount = true);
    
    /**
     * Initialize storage manager without default backend (v2.0).
     * 
     * Initializes manager without creating any backends. Application must
     * manually register backends via register_backend().
     * 
     * @return ESP_OK              Initialization successful
     *         ESP_ERR_INVALID_STATE Already initialized
     * 
     * @note Thread-safe. Can only be called once.
     * @note v2.0 pattern for full control over backend configuration.
     */
    esp_err_t init_multi_backend();
    
    /**
     * Check if manager is initialized.
     * 
     * @return true if initialized, false otherwise
     */
    bool is_initialized() const { return m_initialized; }
    
    /* ========================================================================
     * Backend Management
     * ====================================================================== */
    
    /**
     * Register a storage backend.
     * 
     * Adds backend to registry. Backend will receive file operations for
     * paths matching its mount point prefix.
     * 
     * @param backend  Pointer to backend instance (must remain valid)
     * @param owns     If true, manager will delete backend on deinit
     * @return ESP_OK              Backend registered successfully
     *         ESP_ERR_INVALID_ARG  backend is NULL or mount point invalid
     *         ESP_ERR_INVALID_STATE Mount point overlaps with existing backend
     *         ESP_ERR_NO_MEM       Registry full (max 8 backends)
     * 
     * @note Thread-safe. Can be called after init() or init_multi_backend().
     * @note Delegates to StorageRegistry::register_backend().
     */
    esp_err_t register_backend(StorageBackend* backend, bool owns);
    
    /**
     * Unregister a storage backend.
     * 
     * Removes backend from registry. If backend was registered with owns=true,
     * it will be deleted.
     * 
     * @param mount_point  Mount point of backend to unregister
     * @return ESP_OK              Backend unregistered successfully
     *         ESP_ERR_INVALID_ARG  mount_point is NULL
     *         ESP_ERR_NOT_FOUND    No backend with this mount point
     * 
     * @note Thread-safe. Delegates to StorageRegistry::unregister_backend().
     */
    esp_err_t unregister_backend(const char* mount_point);
    
    /**
     * Get number of registered backends.
     * 
     * @return Number of backends (0 to 8)
     */
    size_t get_backend_count() const;
    
    /**
     * Get backend by index.
     * 
     * @param index  Backend index (0 to get_backend_count()-1)
     * @return Pointer to backend, or NULL if index out of range
     */
    StorageBackend* get_backend(size_t index);
    
    /**
     * Get backend by mount point.
     * 
     * @param mount_point  Mount point to search for
     * @return Pointer to backend, or NULL if not found
     */
    StorageBackend* get_backend_by_mount(const char* mount_point);
    
    /**
     * Get backend for a specific file/directory path.
     * 
     * Resolves the path to the matching backend and optionally returns
     * the path relative to the backend's mount point.
     * 
     * @param path           Absolute path (e.g. "/flash/dir/file.txt")
     * @param relative_path  Optional pointer to receive relative path
     * @return Pointer to matching backend, or NULL if no backend matches
     */
    StorageBackend* get_backend_for_path(const char* path, const char** relative_path = nullptr);
    
    /* ========================================================================
     * Space Information
     * ====================================================================== */
    
    /**
     * Query available storage space.
     * 
     * Routes to appropriate backend based on path prefix. Returns total and
     * free bytes on that backend.
     * 
     * @param path         Absolute path (e.g., "/sd/", "/flash/")
     * @param[out] total_bytes  Total storage capacity (may be NULL)
     * @param[out] free_bytes   Available storage (may be NULL)
     * @return ESP_OK              Space info retrieved
     *         ESP_ERR_INVALID_ARG  path is NULL
     *         ESP_ERR_NOT_FOUND    No backend for path
     *         ESP_ERR_INVALID_STATE Backend not mounted
     *         ESP_FAIL             Query failed
     * 
     * @note Thread-safe. May block briefly.
     */
    esp_err_t get_space_info(const char* path, uint64_t* total_bytes, uint64_t* free_bytes);
    
    /* ========================================================================
     * File Operations
     * ====================================================================== */
    
    /**
     * List directory contents.
     * 
     * Routes to backend, iterates directory, calls callback for each entry.
     * 
     * @param path      Absolute directory path (e.g., "/sd/models/")
     * @param callback  Function called for each entry
     * @param user_data Opaque pointer passed to callback
     * @return ESP_OK              List successful
     *         ESP_ERR_INVALID_ARG  path or callback is NULL
     *         ESP_ERR_NOT_FOUND    Backend or directory not found
     *         ESP_FAIL             List failed
     * 
     * @note Thread-safe. Callback called synchronously.
     */
    esp_err_t list_directory(const char* path,
                            void (*callback)(const char* name, uint64_t size,
                                           bool is_dir, uint32_t timestamp,
                                           void* user_data),
                            void* user_data);
    
    /**
     * Get file statistics.
     * 
     * Routes to backend, returns file size, timestamp, and type.
     * 
     * @param path         Absolute file path (e.g., "/sd/file.txt")
     * @param[out] size         File size in bytes (may be NULL)
     * @param[out] timestamp    Unix timestamp (may be NULL)
     * @param[out] is_dir       true if directory (may be NULL)
     * @return ESP_OK              Stat successful
     *         ESP_ERR_INVALID_ARG  path is NULL
     *         ESP_ERR_NOT_FOUND    Backend or file not found
     *         ESP_FAIL             Stat failed
     * 
     * @note Thread-safe. Non-blocking.
     */
    esp_err_t stat_file(const char* path, uint64_t* size, 
                       uint32_t* timestamp, bool* is_dir);
    
    /**
     * Delete a file.
     * 
     * Routes to backend, deletes file.
     * 
     * @param path  Absolute file path (e.g., "/sd/file.txt")
     * @return ESP_OK              File deleted
     *         ESP_ERR_INVALID_ARG  path is NULL or root
     *         ESP_ERR_NOT_FOUND    Backend or file not found
     *         ESP_ERR_INVALID_STATE Path is a directory
     *         ESP_FAIL             Delete failed
     * 
     * @note Thread-safe. May block.
     */
    esp_err_t delete_file(const char* path);
    
    /**
     * Rename or move a file.
     * 
     * Routes to backend, renames file. Both paths must be on same backend.
     * Cross-backend rename not supported (use copy + delete).
     * 
     * @param old_path  Current absolute path
     * @param new_path  New absolute path
     * @return ESP_OK              Rename successful
     *         ESP_ERR_INVALID_ARG  Paths NULL or on different backends
     *         ESP_ERR_NOT_FOUND    Backend or old_path not found
     *         ESP_ERR_INVALID_STATE new_path already exists
     *         ESP_FAIL             Rename failed
     * 
     * @note Thread-safe. May block.
     * @note Atomic on most filesystems.
     */
    esp_err_t rename_file(const char* old_path, const char* new_path);
    
    /**
     * Create a directory.
     * 
     * Routes to backend, creates directory. Parent must exist.
     * 
     * @param path  Absolute directory path (e.g., "/sd/models/")
     * @return ESP_OK              Directory created
     *         ESP_ERR_INVALID_ARG  path is NULL or root
     *         ESP_ERR_INVALID_STATE Directory already exists
     *         ESP_ERR_NOT_FOUND    Backend or parent not found
     *         ESP_FAIL             Create failed
     * 
     * @note Thread-safe. May block.
     * @note Not recursive - parent must exist.
     */
    esp_err_t create_directory(const char* path);
    
    /**
     * Copy file within backend.
     * 
     * Routes to backend, copies file. Both paths must be on same backend.
     * Cross-backend copy handled by higher layer.
     * 
     * @param src_path  Source absolute path
     * @param dst_path  Destination absolute path
     * @return ESP_OK              Copy successful
     *         ESP_ERR_INVALID_ARG  Paths NULL or on different backends
     *         ESP_ERR_NOT_FOUND    Backend or source not found
     *         ESP_ERR_INVALID_STATE Destination already exists
     *         ESP_ERR_NO_MEM       Out of memory
     *         ESP_FAIL             Copy failed
     * 
     * @note Thread-safe. May block for large files.
     */
    esp_err_t copy_file(const char* src_path, const char* dst_path);
    
    /**
     * Calculate file hash (CRC32).
     * 
     * Routes to backend, computes CRC32 of entire file.
     * 
     * @param path     Absolute file path
     * @param[out] hash     CRC32 hash value
     * @return ESP_OK              Hash calculated
     *         ESP_ERR_INVALID_ARG  path or hash is NULL
     *         ESP_ERR_NOT_FOUND    Backend or file not found
     *         ESP_ERR_NO_MEM       Out of memory
     *         ESP_FAIL             Hash failed
     * 
     * @note Thread-safe. May block for minutes on large files.
     */
    esp_err_t hash_file(const char* path, uint32_t* hash);
    
    /**
     * Delete directory recursively.
     * 
     * Routes to backend, deletes directory and all contents.
     * 
     * @param path  Absolute directory path
     * @return ESP_OK              Directory deleted
     *         ESP_ERR_INVALID_ARG  path is NULL or root
     *         ESP_ERR_NOT_FOUND    Backend or directory not found
     *         ESP_ERR_INVALID_STATE Directory has open files
     *         ESP_FAIL             Delete failed
     * 
     * @note Thread-safe. May block for extended time.
     * @warning DESTRUCTIVE - cannot be undone!
     */
    esp_err_t delete_directory_recursive(const char* path);
    
    /**
     * Validate path format.
     * 
     * Checks path validity and backend existence.
     * 
     * @param path  Path to validate
     * @return ESP_OK              Path valid and backend exists
     *         ESP_ERR_INVALID_ARG  Path malformed
     *         ESP_ERR_NOT_FOUND    No backend for path
     * 
     * @note Thread-safe. Does not check if file exists.
     */
    esp_err_t validate_path(const char* path);
    
private:
    bool m_initialized;                    /**< Initialization state */
    StorageBackend* m_default_backend;     /**< Default SD backend (v1.x compat) */
    bool m_owns_default_backend;           /**< If true, delete default backend */
    StorageRegistry& m_registry;           /**< Reference to singleton registry */
    
    static constexpr const char* TAG = "storage_manager";  /**< Logging tag */
};
