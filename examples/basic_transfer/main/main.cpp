/**
 * @file main.cpp
 * @brief Multi-backend filebridge example with SD, Flash/FAT, LittleFS, and SPIFFS
 *
 * Demonstrates initialization of esp-idf-uart-filebridge and dynamic registration
 * of storage backends according to Kconfig settings.
 *
 * Accessible backends (when enabled in menuconfig):
 * - /sd/     - SD Card (FAT32/exFAT)
 * - /flash/  - Internal flash with FAT + wear leveling
 * - /lfs/    - Internal flash with LittleFS
 * - /spiffs/ - Internal flash with SPIFFS (flat namespace)
 *
 * Tested on: ESP32-P4 with 32MB flash and FT232R @ 3,000,000 baud
 */

#include <stdio.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_idf_uart_filebridge.h"

// Include backend headers
#include "storage_manager.h"
#include "storage_flash_fat.h"
#include "storage_littlefs.h"
#include "storage_spiffs.h"

static const char *TAG = "multi_backend_example";

extern "C" void app_main(void) {
    /* NVS required by some IDF subsystems */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize file bridge with SD backend (default) */
    esp_idf_uart_filebridge_config_t cfg = ESP_IDF_UART_FILEBRIDGE_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_idf_uart_filebridge_init(&cfg));
    
    ESP_LOGI(TAG, "UART file bridge initialized with SD backend at /sd");

    /* Explicitly mount SD to initialize storage manager */
    ESP_LOGI(TAG, "Mounting SD card to initialize storage manager...");
    ret = esp_idf_uart_filebridge_mount_sd();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s (continuing anyway)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD card mounted successfully");
    }

    /* Wait a bit for initialization to complete */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Get storage manager to register additional backends */
    StorageManager* storage_mgr = esp_idf_uart_filebridge_get_storage_manager();
    if (!storage_mgr) {
        ESP_LOGE(TAG, "Failed to get storage manager - backend registration unavailable");
        return;
    }

#ifdef CONFIG_UART_FILEBRIDGE_BACKEND_SPIFFS
    /* Register SPIFFS backend at /spiffs */
    ESP_LOGI(TAG, "Registering SPIFFS backend...");
    StorageSpiffs* spiffs_backend = new StorageSpiffs(
        "/spiffs",                                          // mount point
        CONFIG_UART_FILEBRIDGE_SPIFFS_PARTITION,           // partition label
        CONFIG_UART_FILEBRIDGE_SPIFFS_MAX_FILES,           // max files
        true                                                // format if mount fails
    );
    
    ret = storage_mgr->register_backend(spiffs_backend, true);
    if (ret == ESP_OK) {
        ret = spiffs_backend->mount();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS backend mounted at /spiffs");
        } else {
            ESP_LOGW(TAG, "SPIFFS mount failed: %s (lazy mount will retry on first access)",
                     esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to register SPIFFS backend: %s", esp_err_to_name(ret));
        delete spiffs_backend;
    }
#else
    ESP_LOGI(TAG, "SPIFFS backend disabled in menuconfig");
#endif

#ifdef CONFIG_UART_FILEBRIDGE_BACKEND_FLASH_FAT
    /* Register Flash/FAT backend */
    ESP_LOGI(TAG, "Registering Flash/FAT backend at %s...", CONFIG_UART_FILEBRIDGE_FLASH_FAT_MOUNT_POINT);
    StorageFlashFAT* flash_backend = new StorageFlashFAT(
        CONFIG_UART_FILEBRIDGE_FLASH_FAT_MOUNT_POINT,
        CONFIG_UART_FILEBRIDGE_FLASH_FAT_PARTITION,
        CONFIG_UART_FILEBRIDGE_FLASH_FAT_MAX_FILES
    );
    
    ret = storage_mgr->register_backend(flash_backend, true);
    if (ret == ESP_OK) {
        ret = flash_backend->mount();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Flash/FAT backend mounted at %s", CONFIG_UART_FILEBRIDGE_FLASH_FAT_MOUNT_POINT);
        } else {
            ESP_LOGW(TAG, "Flash/FAT mount failed: %s (will retry on first access)",
                     esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to register Flash/FAT backend: %s", esp_err_to_name(ret));
        delete flash_backend;
    }
#else
    ESP_LOGI(TAG, "Flash/FAT backend disabled in menuconfig");
#endif

#ifdef CONFIG_UART_FILEBRIDGE_BACKEND_LITTLEFS
    /* Register LittleFS backend */
    ESP_LOGI(TAG, "Registering LittleFS backend at %s...", CONFIG_UART_FILEBRIDGE_LITTLEFS_MOUNT_POINT);
    StorageLittleFS* lfs_backend = new StorageLittleFS(
        CONFIG_UART_FILEBRIDGE_LITTLEFS_MOUNT_POINT,
        CONFIG_UART_FILEBRIDGE_LITTLEFS_PARTITION,
        CONFIG_UART_FILEBRIDGE_LITTLEFS_MAX_FILES,
        true             // format if mount fails
    );
    
    ret = storage_mgr->register_backend(lfs_backend, true);
    if (ret == ESP_OK) {
        ret = lfs_backend->mount();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "LittleFS backend mounted at %s", CONFIG_UART_FILEBRIDGE_LITTLEFS_MOUNT_POINT);
        } else {
            ESP_LOGW(TAG, "LittleFS mount failed: %s (will retry on first access)",
                     esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to register LittleFS backend: %s", esp_err_to_name(ret));
        delete lfs_backend;
    }
#else
    ESP_LOGI(TAG, "LittleFS backend disabled in menuconfig");
#endif

    /* Log all registered backends */
    size_t backend_count = storage_mgr->get_backend_count();
    ESP_LOGI(TAG, "Total backends registered: %d", backend_count);
    
    for (size_t i = 0; i < backend_count; i++) {
        StorageBackend* backend = storage_mgr->get_backend(i);
        if (backend) {
            ESP_LOGI(TAG, "  [%d] %s at %s (%s)", i,
                     backend->get_type_name(),
                     backend->get_mount_point(),
                     backend->is_mounted() ? "mounted" : "not mounted");
        }
    }

    ESP_LOGI(TAG, "Multi-backend system ready!");
    ESP_LOGI(TAG, "Test backends with Python CLI:");
    ESP_LOGI(TAG, "  esp-idf-uart-filebridge storage --port COM13");
    ESP_LOGI(TAG, "  esp-idf-uart-filebridge upload test.txt /spiffs/test.txt --port COM13");
    ESP_LOGI(TAG, "  esp-idf-uart-filebridge download /spiffs/test.txt output.txt --port COM13");

    /* Application loop - bridge runs in background RX task */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
