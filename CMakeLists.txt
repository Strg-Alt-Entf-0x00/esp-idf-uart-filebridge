cmake_minimum_required(VERSION 3.16)

# The repository root is the managed ESP-IDF component. Python tools and
# examples are kept alongside the component sources but are not registered.

idf_component_register(
    SRCS
        "src/esp_idf_uart_filebridge.cpp"
        "src/file_protocol.cpp"
        "src/filesystem_manager.cpp"
        "src/internal/storage_registry.cpp"
        "src/internal/storage_manager.cpp"
        "src/internal/compression.cpp"
        "src/backends/storage_sd_fat.cpp"
        "src/backends/storage_flash_fat.cpp"
        "src/backends/storage_littlefs.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src/internal"
        "src/backends"
    REQUIRES
        esp_driver_uart
    PRIV_REQUIRES
        esp_psram
        fatfs
        sdmmc
        vfs
        freertos
        log
        esp_partition
        wear_levelling
        joltwallet__littlefs
        esp_timer
)
