/*
 * UART Multiplexer - Professional Console + Binary Protocol on Single UART
 * 
 * Multiplexes console output (text) and filebridge protocol (binary)
 * on the same UART using COBS framing with packet types.
 * 
 * Frame Format:
 *   [TYPE:1][LENGTH:2][PAYLOAD:N][CRC16:2]  <-- Raw frame
 *   [COBS Encoded][0x00]                     <-- On wire
 * 
 * Types:
 *   0x01 = Console text (logs, prints, debug output)
 *   0x02 = Filebridge binary protocol
 * 
 * Features:
 *   - Zero interference between console and protocol
 *   - Automatic resync on corruption
 *   - CRC16 integrity check
 *   - Predictable overhead (<1%)
 *   - Industrial-grade quality
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "cobs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Packet types
#define MUX_TYPE_CONSOLE    0x01  // Console text output
#define MUX_TYPE_PROTOCOL   0x02  // Binary filebridge protocol

// Maximum payload size (before encoding)
#define MUX_MAX_PAYLOAD     (32768 + 16)  // Match PROTO_MAX_PAYLOAD + overhead

// Frame header size
#define MUX_HEADER_SIZE     5  // Type(1) + Length(2) + CRC16(2)

// Maximum frame size (after encoding + delimiter)
// COBS worst case: input_size + (input_size/254) + 1
// For our max: (32784 + 5) + (32789/254) + 1 + 1 = 32789 + 129 + 2 = 32920
#define MUX_MAX_FRAME_SIZE  33000  // Safe upper bound for COBS encoded frame

/**
 * CRC16-CCITT calculation
 */
static inline uint16_t mux_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    
    while (len--) {
        crc ^= (*data++) << 8;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

/**
 * Encode and frame a packet for transmission
 * 
 * @param type Packet type (MUX_TYPE_CONSOLE or MUX_TYPE_PROTOCOL)
 * @param payload Payload data
 * @param payload_len Payload length
 * @param out_frame Output buffer (must be at least MUX_MAX_FRAME_SIZE bytes)
 * @param out_len Output length (includes final 0x00 delimiter)
 * @return ESP_OK on success
 */
static inline esp_err_t mux_encode_frame(uint8_t type, const uint8_t* payload, 
                                         size_t payload_len, uint8_t* out_frame, 
                                         size_t* out_len) {
    if (payload_len > MUX_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Build raw frame: [TYPE][LENGTH][PAYLOAD][CRC16]
    uint8_t raw_frame[MUX_MAX_PAYLOAD + MUX_HEADER_SIZE];
    uint8_t* ptr = raw_frame;
    
    // Type
    *ptr++ = type;
    
    // Length (16-bit little-endian)
    *ptr++ = payload_len & 0xFF;
    *ptr++ = (payload_len >> 8) & 0xFF;
    
    // Payload
    if (payload_len > 0) {
        memcpy(ptr, payload, payload_len);
        ptr += payload_len;
    }
    
    // CRC16 (over type + length + payload)
    uint16_t crc = mux_crc16(raw_frame, ptr - raw_frame);
    *ptr++ = crc & 0xFF;
    *ptr++ = (crc >> 8) & 0xFF;
    
    size_t raw_len = ptr - raw_frame;
    
    // COBS encode
    size_t encoded_len = cobs_encode(raw_frame, raw_len, out_frame);
    
    // Add delimiter
    out_frame[encoded_len] = 0x00;
    *out_len = encoded_len + 1;
    
    return ESP_OK;
}

/**
 * Decode a received frame
 * 
 * @param encoded_frame Encoded frame (without final 0x00 delimiter)
 * @param encoded_len Length of encoded data
 * @param out_type Output packet type
 * @param out_payload Output payload buffer (must be at least MUX_MAX_PAYLOAD bytes)
 * @param out_payload_len Output payload length
 * @return ESP_OK on success, ESP_ERR_INVALID_CRC if CRC fails
 */
static inline esp_err_t mux_decode_frame(const uint8_t* encoded_frame, size_t encoded_len,
                                         uint8_t* out_type, uint8_t* out_payload,
                                         size_t* out_payload_len) {
    // COBS decode
    uint8_t raw_frame[MUX_MAX_PAYLOAD + MUX_HEADER_SIZE];
    size_t raw_len = cobs_decode(encoded_frame, encoded_len, raw_frame);
    
    if (raw_len < MUX_HEADER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Parse header
    uint8_t type = raw_frame[0];
    uint16_t payload_len = raw_frame[1] | (raw_frame[2] << 8);
    
    if (raw_len != MUX_HEADER_SIZE + payload_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Verify CRC16
    uint16_t received_crc = raw_frame[raw_len - 2] | (raw_frame[raw_len - 1] << 8);
    uint16_t calculated_crc = mux_crc16(raw_frame, raw_len - 2);
    
    if (received_crc != calculated_crc) {
        return ESP_ERR_INVALID_CRC;
    }
    
    // Extract payload
    *out_type = type;
    *out_payload_len = payload_len;
    
    if (payload_len > 0) {
        memcpy(out_payload, raw_frame + 3, payload_len);
    }
    
    return ESP_OK;
}

/**
 * Streaming decoder for incremental frame assembly
 * 
 * Usage:
 *   mux_decoder_t decoder;
 *   mux_decoder_init(&decoder);
 *   
 *   // Feed bytes from UART
 *   for each byte:
 *       if (mux_decoder_feed(&decoder, byte) == ESP_OK) {
 *           // Frame complete!
 *           process_frame(decoder.type, decoder.payload, decoder.payload_len);
 *           mux_decoder_reset(&decoder);
 *       }
 */
typedef struct {
    uint8_t  buffer[MUX_MAX_FRAME_SIZE];
    size_t   index;
    uint8_t  type;
    uint8_t  payload[MUX_MAX_PAYLOAD];
    size_t   payload_len;
} mux_decoder_t;

static inline void mux_decoder_init(mux_decoder_t* decoder) {
    decoder->index = 0;
    decoder->type = 0;
    decoder->payload_len = 0;
}

static inline void mux_decoder_reset(mux_decoder_t* decoder) {
    decoder->index = 0;
}

/**
 * Feed one byte into streaming decoder
 * 
 * @return ESP_OK if frame is complete (check decoder->type and decoder->payload)
 *         ESP_ERR_INVALID_CRC if frame failed CRC check (auto-resets)
 *         ESP_FAIL if still accumulating bytes
 */
static inline esp_err_t mux_decoder_feed(mux_decoder_t* decoder, uint8_t byte) {
    // Delimiter marks end of frame
    if (byte == 0x00) {
        if (decoder->index == 0) {
            // Empty frame, ignore
            return ESP_FAIL;
        }
        
        // Decode frame
        esp_err_t ret = mux_decode_frame(decoder->buffer, decoder->index, 
                                         &decoder->type, decoder->payload, 
                                         &decoder->payload_len);
        
        if (ret != ESP_OK) {
            // CRC error or invalid frame - reset and resync
            decoder->index = 0;
            return ESP_ERR_INVALID_CRC;
        }
        
        // Frame complete!
        return ESP_OK;
    }
    
    // Accumulate byte
    if (decoder->index < MUX_MAX_FRAME_SIZE) {
        decoder->buffer[decoder->index++] = byte;
    } else {
        // Buffer overflow - reset and resync
        decoder->index = 0;
    }
    
    return ESP_FAIL;  // Still accumulating
}

#ifdef __cplusplus
}
#endif
