/*
 * ESP-IDF UART Filebridge - Compression Support
 * 
 * Compression layer using ESP32 ROM miniz (deflate/inflate).
 * Designed to reduce flash writes and extend flash lifetime.
 * 
 * Features:
 * - Zero flash cost (uses ROM miniz)
 * - Auto-detection (test compression, use if beneficial)
 * - Streaming compression/decompression
 * - Statistics tracking (compression ratio, bytes saved)
 * 
 * Auto-Detection Strategy:
 * 1. Check file extension (.jpg, .zip, .gz → skip, already compressed)
 * 2. Compress first block (test compression)
 * 3. If ratio > threshold (50%), use compression
 * 4. Otherwise, store uncompressed
 * 
 * Version: 2.0.0
 * Date: 2026-10-12
 */

#pragma once

#include "esp_err.h"
#include <cstdint>
#include <cstddef>

/**
 * Compression level.
 */
enum CompressionLevel : uint8_t {
    COMPRESS_LEVEL_FAST = 1,      ///< Fast compression, lower ratio
    COMPRESS_LEVEL_DEFAULT = 6,   ///< Balanced speed/ratio
    COMPRESS_LEVEL_BEST = 9       ///< Best compression, slower
};

/**
 * Compression decision result.
 */
enum CompressionDecision : uint8_t {
    COMPRESS_NO = 0,      ///< Don't compress (already compressed, ratio poor)
    COMPRESS_YES = 1,     ///< Compress (good ratio expected)
    COMPRESS_UNKNOWN = 2  ///< Unknown (need to test)
};

/**
 * Compression statistics.
 */
struct CompressionStats {
    size_t bytes_in;          ///< Uncompressed bytes
    size_t bytes_out;         ///< Compressed bytes
    uint32_t compression_ms;  ///< Compression time (milliseconds)
    
    /**
     * Get compression ratio (0.0 = no compression, 1.0 = 100% reduction).
     * 
     * @return Ratio [0.0, 1.0], or 0.0 if bytes_in == 0
     */
    float get_ratio() const {
        if (bytes_in == 0) return 0.0f;
        if (bytes_out >= bytes_in) return 0.0f;
        return (float)(bytes_in - bytes_out) / (float)bytes_in;
    }
    
    /**
     * Get compression percentage (0% = no compression, 100% = full reduction).
     * 
     * @return Percentage [0, 100]
     */
    int get_percentage() const {
        return (int)(get_ratio() * 100.0f);
    }
    
    /**
     * Get bytes saved by compression.
     * 
     * @return Bytes saved, or 0 if no compression benefit
     */
    size_t get_bytes_saved() const {
        if (bytes_out >= bytes_in) return 0;
        return bytes_in - bytes_out;
    }
};

/**
 * Compression namespace - all compression utilities.
 */
namespace Compression {

/**
 * Check if file extension suggests already-compressed data.
 * 
 * Extensions considered pre-compressed: .jpg, .jpeg, .png, .gif, .mp3, .mp4,
 * .zip, .gz, .bz2, .xz, .7z, .rar, .tgz, .bin (models)
 * 
 * @param filename File name or path (extension extracted)
 * @return true if likely already compressed
 * 
 * @note Thread-safe (no state)
 */
bool is_precompressed_extension(const char* filename);

/**
 * Decide if data should be compressed based on auto-detection.
 * 
 * Strategy:
 * 1. Check file extension
 * 2. Compress first block (up to 4KB)
 * 3. If ratio > threshold, recommend compression
 * 
 * @param filename File name (for extension check)
 * @param data Data buffer (at least 1KB recommended for test)
 * @param data_len Data length
 * @param threshold Minimum compression ratio (default: 0.3 = 30% reduction)
 * @return COMPRESS_YES, COMPRESS_NO, or COMPRESS_UNKNOWN
 * 
 * @note Thread-safe (no state)
 * @note Fast test (only first block compressed)
 */
CompressionDecision auto_detect(const char* filename,
                                 const uint8_t* data,
                                 size_t data_len,
                                 float threshold = 0.3f);

/**
 * Compress data buffer (one-shot).
 * 
 * @param src Source data (uncompressed)
 * @param src_len Source data length
 * @param dst Destination buffer (compressed)
 * @param dst_capacity Destination buffer capacity
 * @param compressed_len Output: actual compressed length
 * @param level Compression level (default: COMPRESS_LEVEL_DEFAULT)
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if dst too small
 * 
 * @note Thread-safe (no state)
 * @note Caller must allocate dst buffer (recommend src_len + 512 bytes)
 */
esp_err_t compress(const uint8_t* src,
                   size_t src_len,
                   uint8_t* dst,
                   size_t dst_capacity,
                   size_t* compressed_len,
                   CompressionLevel level = COMPRESS_LEVEL_DEFAULT);

/**
 * Decompress data buffer (one-shot).
 * 
 * @param src Source data (compressed)
 * @param src_len Source data length
 * @param dst Destination buffer (uncompressed)
 * @param dst_capacity Destination buffer capacity
 * @param decompressed_len Output: actual decompressed length
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if dst too small
 * 
 * @note Thread-safe (no state)
 * @note Caller must know uncompressed size (from protocol metadata)
 */
esp_err_t decompress(const uint8_t* src,
                     size_t src_len,
                     uint8_t* dst,
                     size_t dst_capacity,
                     size_t* decompressed_len);

/**
 * Get maximum compressed size for given input size.
 * 
 * Use this to allocate compression destination buffer.
 * Formula: src_len + src_len / 1000 + 12
 * 
 * @param src_len Uncompressed data length
 * @return Maximum possible compressed size
 * 
 * @note Thread-safe (no state)
 */
size_t get_max_compressed_size(size_t src_len);

/**
 * Compress data with statistics tracking.
 * 
 * @param src Source data (uncompressed)
 * @param src_len Source data length
 * @param dst Destination buffer (compressed)
 * @param dst_capacity Destination buffer capacity
 * @param compressed_len Output: actual compressed length
 * @param stats Output: compression statistics
 * @param level Compression level
 * @return ESP_OK on success
 */
esp_err_t compress_with_stats(const uint8_t* src,
                               size_t src_len,
                               uint8_t* dst,
                               size_t dst_capacity,
                               size_t* compressed_len,
                               CompressionStats* stats,
                               CompressionLevel level = COMPRESS_LEVEL_DEFAULT);

} // namespace Compression
