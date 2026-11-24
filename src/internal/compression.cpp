/*
 * ESP-IDF UART Filebridge - Compression Implementation
 */

#include "compression.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "miniz.h"  // ESP-IDF v6.x moved miniz.h to esp_rom/include/
#include <cstring>
#include <cctype>

static const char* TAG = "compression";

// List of pre-compressed file extensions
static const char* PRECOMPRESSED_EXTENSIONS[] = {
    ".jpg", ".jpeg", ".png", ".gif", ".webp",  // Images
    ".mp3", ".mp4", ".m4a", ".aac", ".ogg",    // Audio/Video
    ".zip", ".gz", ".bz2", ".xz", ".7z", ".rar", ".tgz",  // Archives
    ".bin", ".model", ".tflite",               // Binary models (often compressed)
    nullptr  // Sentinel
};

namespace Compression {

/* ============================================================================
 * Extension Checking
 * ========================================================================= */

bool is_precompressed_extension(const char* filename) {
    if (!filename) {
        return false;
    }
    
    // Find extension
    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return false;  // No extension
    }
    
    // Convert to lowercase for comparison
    char ext_lower[16];
    size_t ext_len = strlen(ext);
    if (ext_len >= sizeof(ext_lower)) {
        return false;  // Extension too long
    }
    
    for (size_t i = 0; i < ext_len; i++) {
        ext_lower[i] = tolower(ext[i]);
    }
    ext_lower[ext_len] = '\0';
    
    // Check against pre-compressed list
    for (int i = 0; PRECOMPRESSED_EXTENSIONS[i] != nullptr; i++) {
        if (strcmp(ext_lower, PRECOMPRESSED_EXTENSIONS[i]) == 0) {
            return true;
        }
    }
    
    return false;
}

/* ============================================================================
 * Auto-Detection
 * ========================================================================= */

CompressionDecision auto_detect(const char* filename,
                                 const uint8_t* data,
                                 size_t data_len,
                                 float threshold) {
    // Check extension first
    if (is_precompressed_extension(filename)) {
        ESP_LOGD(TAG, "File %s: pre-compressed extension detected, skipping compression", filename);
        return COMPRESS_NO;
    }
    
    // Need data to test compression
    if (!data || data_len == 0) {
        return COMPRESS_UNKNOWN;
    }
    
    // Test compression on first block (max 4KB for speed)
    size_t test_len = (data_len > 4096) ? 4096 : data_len;
    
    // Allocate test buffer
    size_t test_compressed_max = get_max_compressed_size(test_len);
    uint8_t* test_compressed = (uint8_t*)malloc(test_compressed_max);
    if (!test_compressed) {
        ESP_LOGW(TAG, "Failed to allocate test compression buffer");
        return COMPRESS_UNKNOWN;
    }
    
    // Test compression
    size_t compressed_len = 0;
    esp_err_t ret = compress(data, test_len, test_compressed, test_compressed_max,
                             &compressed_len, COMPRESS_LEVEL_FAST);
    
    free(test_compressed);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Test compression failed");
        return COMPRESS_UNKNOWN;
    }
    
    // Calculate ratio
    float ratio = 0.0f;
    if (test_len > 0 && compressed_len < test_len) {
        ratio = (float)(test_len - compressed_len) / (float)test_len;
    }
    
    // Decide
    if (ratio >= threshold) {
        ESP_LOGD(TAG, "File %s: test compression ratio %.1f%% (threshold %.1f%%), recommending compression",
                 filename ? filename : "unknown", ratio * 100.0f, threshold * 100.0f);
        return COMPRESS_YES;
    } else {
        ESP_LOGD(TAG, "File %s: test compression ratio %.1f%% (threshold %.1f%%), skipping compression",
                 filename ? filename : "unknown", ratio * 100.0f, threshold * 100.0f);
        return COMPRESS_NO;
    }
}

/* ============================================================================
 * Compression
 * ========================================================================= */

size_t get_max_compressed_size(size_t src_len) {
    // miniz worst case: src_len + src_len/1000 + 12
    return src_len + (src_len / 1000) + 12;
}

esp_err_t compress(const uint8_t* src,
                   size_t src_len,
                   uint8_t* dst,
                   size_t dst_capacity,
                   size_t* compressed_len,
                   CompressionLevel level) {
    if (!src || !dst || !compressed_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (src_len == 0) {
        *compressed_len = 0;
        return ESP_OK;
    }
    
    // ESP32-P4 ROM only has low-level tdefl API, not high-level mz_compress2
    // Use tdefl_compress_mem_to_mem instead
    size_t compressed_size = tdefl_compress_mem_to_mem(dst, dst_capacity, src, src_len, level);
    
    if (compressed_size == 0) {
        ESP_LOGE(TAG, "Compression failed (buffer too small or error)");
        return ESP_ERR_INVALID_SIZE;
    }
    
    *compressed_len = compressed_size;
    
    ESP_LOGD(TAG, "Compressed %zu bytes → %zu bytes (%.1f%% reduction)",
             src_len, *compressed_len,
             (src_len > 0) ? ((float)(src_len - *compressed_len) / (float)src_len * 100.0f) : 0.0f);
    
    return ESP_OK;
}

/* ============================================================================
 * Decompression
 * ========================================================================= */

esp_err_t decompress(const uint8_t* src,
                     size_t src_len,
                     uint8_t* dst,
                     size_t dst_capacity,
                     size_t* decompressed_len) {
    if (!src || !dst || !decompressed_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (src_len == 0) {
        *decompressed_len = 0;
        return ESP_OK;
    }
    
    // ESP32-P4 ROM only has low-level tinfl API, not high-level mz_uncompress
    // Use tinfl_decompress_mem_to_mem instead
    size_t decompressed_size = tinfl_decompress_mem_to_mem(dst, dst_capacity, src, src_len, 0);
    
    if (decompressed_size == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG, "Decompression failed (corrupted data or buffer too small)");
        return ESP_ERR_INVALID_CRC;
    }
    
    *decompressed_len = decompressed_size;
    
    ESP_LOGD(TAG, "Decompressed %zu bytes → %zu bytes",
             src_len, *decompressed_len);
    
    return ESP_OK;
}

/* ============================================================================
 * Compression with Statistics
 * ========================================================================= */

esp_err_t compress_with_stats(const uint8_t* src,
                               size_t src_len,
                               uint8_t* dst,
                               size_t dst_capacity,
                               size_t* compressed_len,
                               CompressionStats* stats,
                               CompressionLevel level) {
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize stats
    stats->bytes_in = src_len;
    stats->bytes_out = 0;
    stats->compression_ms = 0;
    
    // Measure compression time
    int64_t start_us = esp_timer_get_time();
    
    // Compress
    esp_err_t ret = compress(src, src_len, dst, dst_capacity, compressed_len, level);
    
    int64_t end_us = esp_timer_get_time();
    stats->compression_ms = (uint32_t)((end_us - start_us) / 1000);
    
    if (ret == ESP_OK) {
        stats->bytes_out = *compressed_len;
        
        ESP_LOGI(TAG, "Compression stats: %zu → %zu bytes (%.1f%%), %u ms",
                 stats->bytes_in, stats->bytes_out,
                 stats->get_ratio() * 100.0f, stats->compression_ms);
    }
    
    return ret;
}

} // namespace Compression
