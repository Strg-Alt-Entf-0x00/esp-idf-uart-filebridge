cmake_minimum_required(VERSION 3.16)

# The repository root is the managed ESP-IDF component. Python tools and
# examples are kept alongside the component sources but are not registered.

# Base sources (always compiled)
set(COMPONENT_SRCS
    "src/esp_idf_uart_filebridge.cpp"
    "src/file_protocol.cpp"
    "src/internal/storage_registry.cpp"
    "src/internal/storage_manager.cpp"
    "src/internal/compression.cpp"
    "src/backends/storage_sd_fat.cpp"
    "src/backends/storage_flash_fat.cpp"
    "src/backends/storage_spiffs.cpp"
)

# Add LittleFS backend only if enabled in menuconfig
if(CONFIG_UART_FILEBRIDGE_BACKEND_LITTLEFS)
    list(APPEND COMPONENT_SRCS "src/backends/storage_littlefs.cpp")
endif()

# Base requirements
set(COMPONENT_REQUIRES esp_driver_uart)

# Base private requirements
set(COMPONENT_PRIV_REQUIRES
    esp_driver_usb_serial_jtag
    esp_psram
    fatfs
    sdmmc
    vfs
    freertos
    log
    esp_partition
    wear_levelling
    esp_timer
    spiffs
    spi_flash
)

# Add LittleFS to requirements only if enabled
if(CONFIG_UART_FILEBRIDGE_BACKEND_LITTLEFS)
    list(APPEND COMPONENT_PRIV_REQUIRES joltwallet__littlefs)
endif()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS
        "include"
        "src/internal"
        "src/backends"
    REQUIRES ${COMPONENT_REQUIRES}
    PRIV_REQUIRES ${COMPONENT_PRIV_REQUIRES}
)
