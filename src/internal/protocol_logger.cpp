/**
 * @file protocol_logger.cpp
 * @brief Thread-safe logging implementation for protocol handlers
 */

#include "protocol_logger.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static const char* TAG = "proto_logger";

// Logger state
static struct {
    QueueHandle_t queue;
    TaskHandle_t task;
    bool initialized;
} s_logger = {NULL, NULL, false};

// Default configuration
static const proto_logger_config_t DEFAULT_CONFIG = {
    .queue_size = 32,
    .task_stack_size = 3072,
    .task_priority = 4
};

/**
 * Map protocol log level to ESP log level
 */
static esp_log_level_t map_log_level(proto_log_level_t level) {
    switch (level) {
        case PROTO_LOG_ERROR:   return ESP_LOG_ERROR;
        case PROTO_LOG_WARN:    return ESP_LOG_WARN;
        case PROTO_LOG_INFO:    return ESP_LOG_INFO;
        case PROTO_LOG_DEBUG:   return ESP_LOG_DEBUG;
        case PROTO_LOG_VERBOSE: return ESP_LOG_VERBOSE;
        default:                return ESP_LOG_NONE;
    }
}

/**
 * Log task - runs in separate context, safe to call ESP_LOG
 */
static void proto_log_task(void* param) {
    proto_log_msg_t msg;
    
    ESP_LOGI(TAG, "Protocol logger task started");
    
    while (s_logger.initialized) {
        // Wait for log messages (blocking)
        if (xQueueReceive(s_logger.queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Now safe to call ESP_LOG - we're in a different task context
            esp_log_level_t esp_level = map_log_level(msg.level);
            
            if (esp_level != ESP_LOG_NONE) {
                esp_log_write(esp_level, msg.tag, "%s\n", msg.message);
            }
        }
    }
    
    ESP_LOGI(TAG, "Protocol logger task stopped");
    vTaskDelete(NULL);
}

bool proto_logger_init(const proto_logger_config_t* config) {
    if (s_logger.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    // Use default config if not provided
    if (!config) {
        config = &DEFAULT_CONFIG;
    }
    
    // Create message queue
    s_logger.queue = xQueueCreate(config->queue_size, sizeof(proto_log_msg_t));
    if (!s_logger.queue) {
        ESP_LOGE(TAG, "Failed to create log queue");
        return false;
    }
    
    // Mark as initialized before creating task
    s_logger.initialized = true;
    
    // Create log task
    BaseType_t ret = xTaskCreate(
        proto_log_task,
        "proto_log",
        config->task_stack_size,
        NULL,
        config->task_priority,
        &s_logger.task
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create log task");
        vQueueDelete(s_logger.queue);
        s_logger.queue = NULL;
        s_logger.initialized = false;
        return false;
    }
    
    ESP_LOGI(TAG, "Protocol logger initialized (queue=%lu, stack=%lu, prio=%lu)",
             config->queue_size, config->task_stack_size, config->task_priority);
    
    return true;
}

void proto_logger_deinit(void) {
    if (!s_logger.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Shutting down protocol logger");
    
    // Mark as not initialized - log task will exit
    s_logger.initialized = false;
    
    // Give task time to exit gracefully
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Clean up queue
    if (s_logger.queue) {
        vQueueDelete(s_logger.queue);
        s_logger.queue = NULL;
    }
    
    s_logger.task = NULL;
}

void proto_log(proto_log_level_t level, const char* tag, const char* format, ...) {
    if (!s_logger.initialized || !s_logger.queue) {
        return;  // Logger not initialized, silently drop
    }
    
    if (!tag || !format) {
        return;  // Invalid parameters
    }
    
    // Prepare log message
    proto_log_msg_t msg;
    msg.level = level;
    
    // Copy tag (truncate if needed)
    strncpy(msg.tag, tag, sizeof(msg.tag) - 1);
    msg.tag[sizeof(msg.tag) - 1] = '\0';
    
    // Format message
    va_list args;
    va_start(args, format);
    vsnprintf(msg.message, sizeof(msg.message), format, args);
    va_end(args);
    
    msg.message[sizeof(msg.message) - 1] = '\0';
    
    // Send to queue (non-blocking)
    if (xQueueSend(s_logger.queue, &msg, 0) != pdTRUE) {
        // Queue full - message dropped (acceptable for logging)
        // Don't log this to avoid recursion!
    }
}
