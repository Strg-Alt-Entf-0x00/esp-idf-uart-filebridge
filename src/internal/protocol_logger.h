/**
 * @file protocol_logger.h
 * @brief Thread-safe logging system for protocol handlers
 * 
 * Industrial-Grade Solution:
 * - Prevents COBS multiplexer reentrancy by decoupling logging from protocol context
 * - Non-blocking queue-based architecture
 * - Separate FreeRTOS task handles actual ESP_LOG calls
 * - Zero performance impact on protocol handlers
 * 
 * @note This is the professional approach used in production RTOS systems
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log levels matching ESP-IDF log levels
 */
typedef enum {
    PROTO_LOG_NONE = 0,
    PROTO_LOG_ERROR,
    PROTO_LOG_WARN,
    PROTO_LOG_INFO,
    PROTO_LOG_DEBUG,
    PROTO_LOG_VERBOSE
} proto_log_level_t;

/**
 * Maximum length of a single log message
 */
#define PROTO_LOG_MAX_MSG_LEN 192

/**
 * Log message structure
 */
typedef struct {
    proto_log_level_t level;
    char tag[16];
    char message[PROTO_LOG_MAX_MSG_LEN];
} proto_log_msg_t;

/**
 * Protocol logger configuration
 */
typedef struct {
    uint32_t queue_size;        ///< Number of messages in queue (default: 32)
    uint32_t task_stack_size;   ///< Stack size for log task (default: 3072)
    uint32_t task_priority;     ///< Priority of log task (default: 4)
} proto_logger_config_t;

/**
 * Initialize protocol logger system
 * 
 * Creates FreeRTOS task and message queue for thread-safe logging.
 * Must be called before any protocol operations.
 * 
 * @param config Logger configuration (NULL for defaults)
 * @return true on success, false on failure
 */
bool proto_logger_init(const proto_logger_config_t* config);

/**
 * Shutdown protocol logger system
 * 
 * Stops log task and frees resources.
 */
void proto_logger_deinit(void);

/**
 * Log a message (non-blocking)
 * 
 * @param level Log level
 * @param tag Log tag (max 15 chars)
 * @param format Printf-style format string
 * @param ... Variable arguments
 * 
 * @note If queue is full, message is dropped (non-blocking guarantee)
 */
void proto_log(proto_log_level_t level, const char* tag, const char* format, ...);

/**
 * Convenience macros for different log levels
 */
#define PROTO_LOGE(tag, format, ...) proto_log(PROTO_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define PROTO_LOGW(tag, format, ...) proto_log(PROTO_LOG_WARN, tag, format, ##__VA_ARGS__)
#define PROTO_LOGI(tag, format, ...) proto_log(PROTO_LOG_INFO, tag, format, ##__VA_ARGS__)
#define PROTO_LOGD(tag, format, ...) proto_log(PROTO_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#define PROTO_LOGV(tag, format, ...) proto_log(PROTO_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
