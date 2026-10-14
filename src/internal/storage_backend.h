/*
 * ESP-IDF UART Filebridge - Storage Backend Abstract Interface
 * 
 * Pure virtual interface for all storage backends (SD/FAT, Flash/FAT, LittleFS, SPIFFS).
 * This interface enables polymorphic backend management and path-based routing.
 * 
 * Design Principles:
 * - Backend-agnostic: Protocol layer doesn't know about specific backends
 * - Capability-based: Operations check capabilities before execution
 * - POSIX-friendly: Uses standard FILE* and VFS paths
 * - Thread-safe: Implementations must handle concurrent access
 * 
 * Version: 2.0.0
 * Date: 2026-10-12
 */

#pragma once

#include <cstdint>
#include <cstdio>
#include "esp_err.h"

/**
 * Storage backend capability flags.
 * 
 * Backends declare their capabilities using these flags. Operations check
 * capabilities before execution and return ERR_NOT_SUPPORTED for missing
 * capabilities.
 * 
 * Example:
 *   if (!(backend->get_capabilities() & CAP_DIRECTORIES)) {
 *       return ESP_ERR_NOT_SUPPORTED;
 *   }
 */
enum StorageCapability : uint32_t {
    CAP_NONE              = 0,
    CAP_READ              = (1 << 0),   /**< Read files */
    CAP_WRITE             = (1 << 1),   /**< Write files */
    CAP_DIRECTORIES       = (1 << 2),   /**< Create/list directories */
    CAP_RECURSIVE_DELETE  = (1 << 3),   /**< Delete directories recursively */
    CAP_RENAME            = (1 << 4),   /**< Rename/move files */
    CAP_COPY              = (1 << 5),   /**< Copy files within backend */
    CAP_HASH              = (1 << 6),   /**< Calculate file hash (CRC32) */
    CAP_FORMAT            = (1 << 7),   /**< Format filesystem */
    CAP_TIMESTAMPS        = (1 << 8),   /**< File timestamps supported */
    CAP_LARGE_FILES       = (1 << 9),   /**< Files >4GB supported */
    CAP_COMPRESSION       = (1 << 10),  /**< Compression supported */
    CAP_WEAR_LEVELING     = (1 << 11),  /**< Wear leveling active */
    
    CAP_ALL               = 0xFFFFFFFF  /**< All capabilities */
};

/**
 * Storage backend type identifier.
 * 
 * Used for protocol communication and capability reporting.
 * Does not affect behavior (use capability flags instead).
 */
enum class StorageType : uint8_t {
    SD_FAT        = 0x01,  /**< SD card with FAT32/exFAT */
    FLASH_FAT     = 0x02,  /**< Internal flash with FAT + wear leveling */
    LITTLEFS      = 0x03,  /**< LittleFS on internal flash */
    SPIFFS        = 0x04,  /**< SPIFFS on internal flash */
    CUSTOM        = 0xFF   /**< Custom/application-specific backend */
};

/**
 * Backend information structure for protocol reporting.
 * 
 * Sent to host via CMD_STORAGE_INFO to describe backend characteristics.
 * All fields must be populated by backend implementation.
 */
#pragma pack(push, 1)
struct StorageBackendInfo {
    char mount_point[64];       /**< VFS mount point (e.g., "/sd", "/flash") - MUST be 64 bytes for protocol compatibility */
    uint8_t type;               /**< Backend type identifier (1 byte) */
    char type_name[32];         /**< Human-readable type (e.g., "SD/FAT32") */
    uint32_t capabilities;      /**< Capability flags (StorageCapability) */
    uint8_t mounted;            /**< Current mount state (1 byte, 0=false, 1=true) */
    uint64_t total_bytes;       /**< Total storage capacity (0 if unavailable) */
    uint64_t free_bytes;        /**< Available storage (0 if unavailable) */
};
#pragma pack(pop)

static_assert(sizeof(StorageBackendInfo) == 118, "StorageBackendInfo must be exactly 118 bytes");

/**
 * Abstract storage backend interface.
 * 
 * All storage backends (SD/FAT, Flash/FAT, LittleFS, SPIFFS) implement
 * this interface. The StorageRegistry routes paths to backends, and
 * operations are executed polymorphically.
 * 
 * Implementation Requirements:
 * - All methods must be thread-safe or document thread-safety requirements
 * - Null pointer arguments must be handled (return ESP_ERR_INVALID_ARG)
 * - Unsupported operations must return ESP_ERR_NOT_SUPPORTED
 * - Error logging must occur before returning error codes
 * - Resources must be cleaned up via destructor (RAII)
 * 
 * Lifecycle:
 * 1. Construct backend with mount point
 * 2. Register with StorageRegistry
 * 3. mount() called (lazy or eager)
 * 4. File operations via interface methods
 * 5. unmount() called before destruction
 * 6. Destructor cleans up resources
 * 
 * Example:
 *   StorageSDFAT* sd = new StorageSDFAT("/sd");
 *   sd->mount();
 *   FILE* file;
 *   sd->open_file("/file.txt", "r", &file);
 *   // ... use file ...
 *   fclose(file);
 *   sd->unmount();
 *   delete sd;
 */
class StorageBackend {
public:
    virtual ~StorageBackend() = default;
    
    /* ========================================================================
     * Lifecycle Management
     * ====================================================================== */
    
    /**
     * Mount the storage backend.
     * 
     * Performs backend-specific mount operations (e.g., initialize SD card,
     * mount VFS filesystem). Must be idempotent - calling mount() on an
     * already-mounted backend should succeed immediately.
     * 
     * @return ESP_OK              Mount successful or already mounted
     *         ESP_ERR_NOT_FOUND   Storage device not found (e.g., no SD card)
     *         ESP_ERR_INVALID_STATE Backend already mounted by another owner
     *         ESP_FAIL             Mount failed (check logs for details)
     * 
     * @note Thread-safe. Multiple tasks may call mount() concurrently.
     * @note Implementations should use internal mutex for mount state.
     */
    virtual esp_err_t mount() = 0;
    
    /**
     * Unmount the storage backend.
     * 
     * Flushes pending writes, closes open handles, and unmounts VFS filesystem.
     * Must be idempotent - unmounting an already-unmounted backend should
     * succeed immediately.
     * 
     * @return ESP_OK              Unmount successful or already unmounted
     *         ESP_ERR_INVALID_STATE Active file handles exist (cannot unmount)
     *         ESP_FAIL             Unmount failed (check logs for details)
     * 
     * @note Thread-safe. Blocks until all pending operations complete.
     * @warning Unmounting with active file handles may cause data loss!
     */
    virtual esp_err_t unmount() = 0;
    
    /**
     * Check if backend is currently mounted.
     * 
     * @return true if mounted, false otherwise
     * 
     * @note Thread-safe. Non-blocking query.
     */
    virtual bool is_mounted() const = 0;
    
    /* ========================================================================
     * Backend Information
     * ====================================================================== */
    
    /**
     * Get backend capabilities.
     * 
     * Returns a bitmask of StorageCapability flags indicating which operations
     * this backend supports. Operations should check capabilities before
     * execution.
     * 
     * @return Bitmask of StorageCapability flags
     * 
     * @note Thread-safe. Capabilities are static and never change.
     * @note Must return consistent value across all calls.
     */
    virtual uint32_t get_capabilities() const = 0;
    
    /**
     * Get human-readable backend type name.
     * 
     * Used for logging and protocol reporting. Should be short and descriptive.
     * 
     * @return Type name string (e.g., "SD/FAT32", "Flash/LittleFS")
     * 
     * @note Thread-safe. String must remain valid for backend lifetime.
     * @note Returned pointer must be const static or member variable.
     */
    virtual const char* get_type_name() const = 0;
    
    /**
     * Get VFS mount point.
     * 
     * Returns the absolute VFS path where this backend is mounted.
     * Used by StorageRegistry for path routing.
     * 
     * @return Mount point string (e.g., "/sd", "/flash")
     * 
     * @note Thread-safe. Mount point never changes after construction.
     * @note Must return valid pointer even if not mounted.
     * @note Returned pointer must be const static or member variable.
     */
    virtual const char* get_mount_point() const = 0;
    
    /**
     * Get backend type identifier.
     * 
     * Used for protocol communication (CMD_STORAGE_INFO).
     * 
     * @return StorageType enum value
     * 
     * @note Thread-safe. Type never changes after construction.
     */
    virtual StorageType get_type() const = 0;
    
    /**
     * Get detailed backend information.
     * 
     * Populates StorageBackendInfo structure with current backend state.
     * Used for CMD_STORAGE_INFO protocol response.
     * 
     * @param[out] info  Pointer to StorageBackendInfo structure
     * @return ESP_OK              Info populated successfully
     *         ESP_ERR_INVALID_ARG  info is NULL
     *         ESP_ERR_INVALID_STATE Backend not mounted (some fields unavailable)
     * 
     * @note Thread-safe. May block briefly to query storage.
     * @note total_bytes and free_bytes may be 0 if unavailable.
     */
    virtual esp_err_t get_backend_info(StorageBackendInfo* info) = 0;
    
    /* ========================================================================
     * Space Information
     * ====================================================================== */
    
    /**
     * Query available storage space.
     * 
     * Returns total and free bytes on the storage device. Used for disk space
     * checks and protocol reporting.
     * 
     * @param[out] total_bytes  Total storage capacity in bytes (may be NULL)
     * @param[out] free_bytes   Available storage in bytes (may be NULL)
     * @return ESP_OK              Space info retrieved successfully
     *         ESP_ERR_NOT_SUPPORTED Backend doesn't support space queries
     *         ESP_ERR_INVALID_STATE Backend not mounted
     *         ESP_FAIL             Query failed (check logs)
     * 
     * @note Thread-safe. May block briefly to query filesystem.
     * @note Both output pointers may be NULL (no-op, returns capability check).
     * @note Values are approximate and may change between calls.
     */
    virtual esp_err_t get_space_info(uint64_t* total_bytes, uint64_t* free_bytes) = 0;
    
    /* ========================================================================
     * File Operations
     * ====================================================================== */
    
    /**
     * Open a file on this backend.
     * 
     * Opens file using standard fopen() modes. Path is relative to backend's
     * mount point (e.g., "/file.txt" not "/sd/file.txt").
     * 
     * @param path  Relative file path (must not be NULL)
     * @param mode  fopen() mode string ("r", "w", "a", "r+", etc.)
     * @param[out] out  Receives FILE* pointer on success
     * @return ESP_OK              File opened successfully
     *         ESP_ERR_INVALID_ARG  path, mode, or out is NULL
     *         ESP_ERR_NOT_FOUND    File doesn't exist (mode "r")
     *         ESP_ERR_NO_MEM       Out of memory
     *         ESP_FAIL             Open failed (check logs)
     * 
     * @note Thread-safe. Multiple files can be open concurrently.
     * @note Caller must fclose() the returned FILE* when done.
     * @note Path must be normalized (no "..", no trailing slashes).
     * @warning FILE* must be closed before unmounting backend!
     */
    virtual esp_err_t open_file(const char* path, const char* mode, FILE** out) = 0;
    
    /**
     * Get file statistics.
     * 
     * Returns file size, timestamp, and type (file vs directory).
     * Path is relative to backend mount point.
     * 
     * @param path      Relative file/directory path (must not be NULL)
     * @param[out] size      File size in bytes (may be NULL)
     * @param[out] timestamp Unix timestamp (may be NULL, 0 if unsupported)
     * @param[out] is_dir    true if directory, false if file (may be NULL)
     * @return ESP_OK              Stat successful
     *         ESP_ERR_INVALID_ARG  path is NULL
     *         ESP_ERR_NOT_FOUND    Path doesn't exist
     *         ESP_FAIL             Stat failed (check logs)
     * 
     * @note Thread-safe. Non-blocking query.
     * @note Output pointers may be NULL (skip that field).
     * @note Timestamp may be 0 if backend doesn't support timestamps.
     */
    virtual esp_err_t stat_file(const char* path, uint64_t* size, 
                                uint32_t* timestamp, bool* is_dir) = 0;
    
    /**
     * Delete a file.
     * 
     * Removes file from storage. Path is relative to backend mount point.
     * Use delete_directory_recursive() for directories.
     * 
     * @param path  Relative file path (must not be NULL)
     * @return ESP_OK              File deleted successfully
     *         ESP_ERR_INVALID_ARG  path is NULL
     *         ESP_ERR_NOT_FOUND    File doesn't exist
     *         ESP_ERR_INVALID_STATE Path is a directory (use delete_directory_recursive)
     *         ESP_FAIL             Delete failed (check logs)
     * 
     * @note Thread-safe. May block during delete operation.
     * @note Cannot delete open files (close first).
     */
    virtual esp_err_t delete_file(const char* path) = 0;
    
    /**
     * Rename or move a file.
     * 
     * Renames file or moves it to a different directory within this backend.
     * Both paths are relative to backend mount point.
     * Cross-backend rename is not supported (use copy + delete).
     * 
     * @param old_path  Current relative path (must not be NULL)
     * @param new_path  New relative path (must not be NULL)
     * @return ESP_OK              Rename successful
     *         ESP_ERR_INVALID_ARG  old_path or new_path is NULL
     *         ESP_ERR_NOT_FOUND    old_path doesn't exist
     *         ESP_ERR_INVALID_STATE new_path already exists
     *         ESP_ERR_NOT_SUPPORTED Backend doesn't support rename (SPIFFS)
     *         ESP_FAIL             Rename failed (check logs)
     * 
     * @note Thread-safe. May block during rename operation.
     * @note Atomic on most filesystems (FAT, LittleFS).
     * @note Both paths must be on same backend (enforced by registry).
     */
    virtual esp_err_t rename_file(const char* old_path, const char* new_path) = 0;
    
    /* ========================================================================
     * Directory Operations
     * ====================================================================== */
    
    /**
     * Create a directory.
     * 
     * Creates a directory at the specified path. Path is relative to backend
     * mount point. Parent directories must exist (not recursive).
     * 
     * @param path  Relative directory path (must not be NULL)
     * @return ESP_OK              Directory created successfully
     *         ESP_ERR_INVALID_ARG  path is NULL
     *         ESP_ERR_INVALID_STATE Directory already exists
     *         ESP_ERR_NOT_FOUND    Parent directory doesn't exist
     *         ESP_ERR_NOT_SUPPORTED Backend doesn't support directories (SPIFFS)
     *         ESP_FAIL             Create failed (check logs)
     * 
     * @note Thread-safe. May block during create operation.
     * @note Use FilesystemManager for recursive directory creation.
     */
    virtual esp_err_t create_directory(const char* path) = 0;
    
    /**
     * List directory contents.
     * 
     * Iterates over all entries in directory, calling callback for each entry.
     * Path is relative to backend mount point. Callback receives entry name
     * (not full path), size, type, and timestamp.
     * 
     * @param path      Relative directory path (must not be NULL)
     * @param callback  Callback function for each entry (must not be NULL)
     * @param user_data Opaque pointer passed to callback (may be NULL)
     * @return ESP_OK              List successful
     *         ESP_ERR_INVALID_ARG  path or callback is NULL
     *         ESP_ERR_NOT_FOUND    Directory doesn't exist
     *         ESP_ERR_INVALID_STATE Path is a file, not a directory
     *         ESP_ERR_NOT_SUPPORTED Backend doesn't support directories
     *         ESP_FAIL             List failed (check logs)
     * 
     * @note Thread-safe. May block during iteration.
     * @note Callback is called synchronously (no async iteration).
     * @note Callback must not perform long operations (holds lock).
     * 
     * Callback signature:
     *   void callback(const char* name, uint64_t size, bool is_dir,
     *                 uint32_t timestamp, void* user_data)
     */
    virtual esp_err_t list_directory(const char* path,
                                     void (*callback)(const char* name, uint64_t size,
                                                     bool is_dir, uint32_t timestamp,
                                                     void* user_data),
                                     void* user_data) = 0;
    
    /**
     * Delete directory recursively.
     * 
     * Removes directory and all its contents (files and subdirectories).
     * Path is relative to backend mount point.
     * 
     * @param path  Relative directory path (must not be NULL)
     * @return ESP_OK              Directory deleted successfully
     *         ESP_ERR_INVALID_ARG  path is NULL or empty (cannot delete root)
     *         ESP_ERR_NOT_FOUND    Directory doesn't exist
     *         ESP_ERR_INVALID_STATE Directory contains open files
     *         ESP_ERR_NOT_SUPPORTED Backend doesn't support recursive delete
     *         ESP_FAIL             Delete failed (check logs)
     * 
     * @note Thread-safe. May block for extended time on large directories.
     * @note DANGEROUS: Cannot be undone. Use with caution.
     * @note Cannot delete backend mount root (path must not be empty).
     * @warning Ensure no files are open before calling!
     */
    virtual esp_err_t delete_directory_recursive(const char* path) = 0;
    
    /* ========================================================================
     * Advanced Operations
     * ====================================================================== */
    
    /**
     * Copy file within backend.
     * 
     * Copies file from source to destination within this backend. Both paths
     * are relative to backend mount point. Cross-backend copy is not supported
     * (handled by higher layer).
     * 
     * @param src_path  Source relative path (must not be NULL)
     * @param dst_path  Destination relative path (must not be NULL)
     * @return ESP_OK              Copy successful
     *         ESP_ERR_INVALID_ARG  src_path or dst_path is NULL
     *         ESP_ERR_NOT_FOUND    Source doesn't exist
     *         ESP_ERR_INVALID_STATE Destination already exists
     *         ESP_ERR_NO_MEM       Out of memory
     *         ESP_FAIL             Copy failed (check logs)
     * 
     * @note Thread-safe. May block for extended time on large files.
     * @note Copies file contents and preserves size (not timestamp).
     * @note Uses buffered copy (no full-file memory allocation).
     */
    virtual esp_err_t copy_file(const char* src_path, const char* dst_path) = 0;
    
    /**
     * Calculate file hash (CRC32).
     * 
     * Computes CRC32 hash of entire file contents. Path is relative to
     * backend mount point. Used for upload verification.
     * 
     * @param path     Relative file path (must not be NULL)
     * @param[out] hash     CRC32 hash value
     * @return ESP_OK              Hash calculated successfully
     *         ESP_ERR_INVALID_ARG  path or hash is NULL
     *         ESP_ERR_NOT_FOUND    File doesn't exist
     *         ESP_ERR_NO_MEM       Out of memory for read buffer
     *         ESP_FAIL             Hash failed (check logs)
     * 
     * @note Thread-safe. Blocks while reading entire file.
     * @note May take seconds for large files (100+ MB).
     * @note Uses buffered reading (no full-file memory allocation).
     * @note CRC32 uses IEEE 802.3 polynomial (same as protocol).
     */
    virtual esp_err_t hash_file(const char* path, uint32_t* hash) = 0;
    
    /**
     * Format filesystem (DESTRUCTIVE).
     * 
     * Erases all data and creates fresh filesystem. Must be unmounted first.
     * This operation cannot be undone.
     * 
     * @return ESP_OK              Format successful
     *         ESP_ERR_INVALID_STATE Backend is mounted (unmount first)
     *         ESP_ERR_NOT_SUPPORTED Backend doesn't support format
     *         ESP_FAIL             Format failed (check logs)
     * 
     * @note Thread-safe. Blocks for extended time (seconds to minutes).
     * @warning DESTRUCTIVE: All data will be lost permanently!
     * @warning Should require user confirmation before calling.
     * @note Some backends (app-owned mounts) should always return ERR_NOT_SUPPORTED.
     */
    virtual esp_err_t format() = 0;
};

