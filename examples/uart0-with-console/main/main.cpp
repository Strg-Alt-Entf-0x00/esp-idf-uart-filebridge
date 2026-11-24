/**
 * @file main.cpp  
 * @brief UART0 Multiplexed Example - Console AND Filebridge on one port
 *
 * This example shows Console (115200 baud) AND Filebridge (115200 baud) 
 * running on the SAME UART0 port, multiplexed via frame-based protocol.
 *
 * Hardware: ESP32-S3 with built-in CH340 USB-UART bridge
 * Connection: Single USB cable to PC (one COM port for everything)
 * Console: Text output @ 115200 baud
 * Filebridge: Binary frames @ 115200 baud (same speed!)
 *
 * The Python tool automatically filters console text from binary frames.
 * Both protocols share the same baud rate for stability.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_idf_uart_filebridge.h"
#include "protocol_defs.h"  // For CMD_BOOT_READY
#include "protocol_logger.h"  // Industrial-grade thread-safe logging

// ESP32-S3/C3/C6/P4: USB-Serial-JTAG registers for DTR reset disable
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32P4
#include "soc/usb_serial_jtag_reg.h"
#include "soc/usb_serial_jtag_struct.h"
#endif

// Storage backends
#include "storage_manager.h"
#include "storage_spiffs.h"
#include "storage_flash_fat.h"
#include "storage_littlefs.h"

static const char* TAG = "uart0_multiplex";

extern "C" void app_main(void)
{
    printf("\n");
    printf("=== ESP32-S3 UART0 Multiplexed Mode ===\n");
    printf("Console AND Filebridge on UART0 @ COM6\n");
    printf("Baud Rate: 115200 (shared for both)\n");
    printf("Protocol:  Frame-based multiplexing\n\n");

    // ========================================================================
    // INDUSTRIAL-GRADE FIX: Disable DTR/RTS hardware reset
    // ========================================================================
    // Problem: Windows/pyserial toggles DTR when opening COM port, resetting ESP32
    // Solution: Disable hardware recognition of DTR/RTS reset sequences
    // Reference: ESP32 TRM, USB-Serial-JTAG Chapter
    // ========================================================================
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32P4
    printf("🛡️  Disabling DTR/RTS hardware reset...\n");
    
    // Use direct register access - USB_SERIAL_JTAG_CONF0_REG bit 2
    // This prevents ESP32 from interpreting DTR/RTS signals as reset commands
    REG_SET_BIT(USB_SERIAL_JTAG_CONF0_REG, BIT(2));  // Disable chip reset via DTR/RTS
    
    printf("✅ DTR/RTS reset disabled - COM port can open/close without reset\n\n");
#else
    printf("⚠️  DTR reset disable not available on this chip\n");
    printf("   Solution: Add 10µF capacitor between EN and GND\n\n");
#endif

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ========================================================================
    // INDUSTRIAL-GRADE: Initialize Protocol Logger
    // ========================================================================
    // Separate FreeRTOS task for logging prevents COBS multiplexer reentrancy
    // This is the professional approach used in production RTOS systems
    // ========================================================================
    ESP_LOGI(TAG, "Initializing protocol logger...");
    if (!proto_logger_init(NULL)) {  // NULL = use default config
        ESP_LOGE(TAG, "Failed to initialize protocol logger!");
        return;
    }
    ESP_LOGI(TAG, "Protocol logger ready (queue-based, thread-safe)");

    // Configure filebridge for UART0 @ 115200 (same as console!)
    esp_idf_uart_filebridge_config_t cfg = ESP_IDF_UART_FILEBRIDGE_CONFIG_DEFAULT();
    cfg.mount_sd_own = false;
    cfg.uart_num = UART_NUM_0;    // UART0 (same as console!)
    cfg.tx_pin = 43;              // ESP32-S3 UART0 default TX
    cfg.rx_pin = 44;              // ESP32-S3 UART0 default RX
    cfg.baud_rate = 115200;       // SAME as console - must match!
    cfg.rts_pin = -1;             // No flow control
    cfg.cts_pin = -1;

    ESP_LOGI(TAG, "Initializing filebridge on UART0...");
    ESP_ERROR_CHECK(esp_idf_uart_filebridge_init(&cfg));

    // Initialize storage manager
    ret = esp_idf_uart_filebridge_init_storage_manager();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Storage manager init failed: %s", esp_err_to_name(ret));
        return;
    }

    StorageManager* mgr = esp_idf_uart_filebridge_get_storage_manager();
    if (!mgr) {
        ESP_LOGE(TAG, "Failed to get storage manager");
        return;
    }

    ESP_LOGI(TAG, "Registering storage backends...");

    // Register SPIFFS
    StorageSpiffs* spiffs = new StorageSpiffs("/spiffs", "spiffs", 5, true);
    if (mgr->register_backend(spiffs, true) == ESP_OK) {
        spiffs->mount();
        ESP_LOGI(TAG, "SPIFFS mounted at /spiffs");
    }

    // Register Flash/FAT
    StorageFlashFAT* flash = new StorageFlashFAT("/flash", "flash", 5);
    if (mgr->register_backend(flash, true) == ESP_OK) {
        flash->mount();
        ESP_LOGI(TAG, "Flash/FAT mounted at /flash");
    }

    // Register LittleFS
    StorageLittleFS* lfs = new StorageLittleFS("/lfs", "littlefs", 5, true);
    if (mgr->register_backend(lfs, true) == ESP_OK) {
        lfs->mount();
        ESP_LOGI(TAG, "LittleFS mounted at /lfs");
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== UART0 Multiplexed Filebridge Ready ===");
    ESP_LOGI(TAG, "Console output: You see this text");
    ESP_LOGI(TAG, "Filebridge: Binary protocol active");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test from PC:");
    ESP_LOGI(TAG, "  python -m esp_idf_uart_filebridge storage --port COM6");
    ESP_LOGI(TAG, "  python -m esp_idf_uart_filebridge upload test.txt /spiffs/ --port COM6");
    ESP_LOGI(TAG, "");
    
    // BOOT_READY is sent automatically after first HELLO in file_protocol.cpp
    // No manual call needed here - single point of truth for deterministic sync
}
