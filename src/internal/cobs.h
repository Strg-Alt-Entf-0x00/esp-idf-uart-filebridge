/*
 * COBS (Consistent Overhead Byte Stuffing) Implementation
 * 
 * Simple, efficient framing for UART multiplexing
 * Based on the algorithm by Stuart Cheshire and Mary Baker (1999)
 * 
 * License: MIT
 * Industrial-grade quality for ESP32 production use
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * COBS Encoding
 * 
 * Encodes data such that no 0x00 bytes appear in the output.
 * The delimiter 0x00 can then be used to separate packets.
 * 
 * Overhead: At most 1 byte per 254 bytes of input, plus 1 byte.
 * 
 * @param src Source data to encode
 * @param src_len Length of source data
 * @param dst Destination buffer (must be at least src_len + (src_len/254) + 2 bytes)
 * @return Length of encoded data (excludes the final 0x00 delimiter)
 */
static inline size_t cobs_encode(const uint8_t* src, size_t src_len, uint8_t* dst) {
    const uint8_t* src_end = src + src_len;
    uint8_t* dst_code = dst++;
    uint8_t* dst_start = dst;
    uint8_t code = 1;
    
    while (src < src_end) {
        uint8_t byte = *src++;
        
        if (byte == 0) {
            // Write code, start new code block
            *dst_code = code;
            dst_code = dst++;
            code = 1;
        } else {
            // Copy data byte
            *dst++ = byte;
            code++;
            
            if (code == 0xFF) {
                // Code byte maxed out, start new block
                *dst_code = code;
                dst_code = dst++;
                code = 1;
            }
        }
    }
    
    // Write final code byte
    *dst_code = code;
    
    return dst - dst_start + 1;  // +1 for initial code byte
}

/**
 * COBS Decoding
 * 
 * Decodes COBS-encoded data back to original form.
 * 
 * @param src Encoded source data (without final 0x00 delimiter)
 * @param src_len Length of encoded data
 * @param dst Destination buffer (must be at least src_len bytes)
 * @return Length of decoded data, or 0 if input is invalid
 */
static inline size_t cobs_decode(const uint8_t* src, size_t src_len, uint8_t* dst) {
    if (src_len == 0) {
        return 0;
    }
    
    const uint8_t* src_end = src + src_len;
    uint8_t* dst_start = dst;
    
    while (src < src_end) {
        uint8_t code = *src++;
        
        // Invalid code byte
        if (code == 0) {
            return 0;
        }
        
        // Copy data bytes
        for (uint8_t i = 1; i < code && src < src_end; i++) {
            *dst++ = *src++;
        }
        
        // Insert zero if not at end and code wasn't 0xFF
        if (code < 0xFF && src < src_end) {
            *dst++ = 0;
        }
    }
    
    return dst - dst_start;
}

/**
 * Calculate maximum encoded size
 * 
 * @param src_len Original data length
 * @return Maximum size after COBS encoding (excluding delimiter)
 */
static inline size_t cobs_encode_max_size(size_t src_len) {
    return src_len + (src_len / 254) + 1;
}

#ifdef __cplusplus
}
#endif
