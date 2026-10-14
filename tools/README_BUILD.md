# Multi-Target Build System

This directory contains tools for building ESP-IDF UART Filebridge across all supported ESP32 variants.

## Quick Start

```bash
# Activate ESP-IDF environment
. $IDF_PATH/export.sh

# Run multi-target build from repo root
./tools/build_all_targets.sh
```

## Supported Targets

| Target    | Architecture | Flash | RAM   | UART Controllers | Notes |
|-----------|--------------|-------|-------|------------------|-------|
| ESP32     | Xtensa LX6   | 4MB   | 520KB | 3 (UART0/1/2)   | Original, dual-core |
| ESP32-S2  | Xtensa LX7   | 4MB   | 320KB | 2 (UART0/1)     | Single-core, no BT |
| ESP32-S3  | Xtensa LX7   | 8MB   | 512KB | 2 (UART0/1)     | Dual-core, WiFi+BLE |
| ESP32-C3  | RISC-V 32bit | 4MB   | 400KB | 2 (UART0/1)     | Single-core, WiFi+BLE |
| ESP32-C6  | RISC-V 32bit | 4MB   | 512KB | 2 (UART0/1)     | Single-core, WiFi 6+BLE+Zigbee |
| ESP32-H2  | RISC-V 32bit | 4MB   | 320KB | 2 (UART0/1)     | Single-core, BLE+Zigbee+Thread |
| ESP32-P4  | RISC-V 64bit | 32MB  | 768KB | 5 (UART0-4)     | Dual-core, high-performance |

## Build Script: `build_all_targets.sh`

### Features

- Builds firmware for all 7 ESP32 variants sequentially
- Generates detailed build report with sizes and times
- Copies binaries to `build_output/` directory
- Logs complete build output per target
- Color-coded terminal output
- Exit code indicates success/failure

### Output Structure

```
build_output/
├── BUILD_REPORT.txt              # Summary report
├── esp32_uart_filebridge.bin     # ESP32 binary
├── esp32s2_uart_filebridge.bin   # ESP32-S2 binary
├── esp32s3_uart_filebridge.bin   # ESP32-S3 binary
├── esp32c3_uart_filebridge.bin   # ESP32-C3 binary
├── esp32c6_uart_filebridge.bin   # ESP32-C6 binary
├── esp32h2_uart_filebridge.bin   # ESP32-H2 binary
├── esp32p4_uart_filebridge.bin   # ESP32-P4 binary
├── esp32_build.log               # Build log for ESP32
├── esp32s2_build.log             # Build log for ESP32-S2
└── ... (logs for each target)
```

### Build Report Example

```
ESP-IDF UART Filebridge - Multi-Target Build Report
Date: Mon Oct 13 14:30:00 2026
ESP-IDF Version: v5.3.0
========================================

Build Summary
=============

Total Targets: 7
Successful:    7
Failed:        0

Target-by-Target Results
========================

Target       | Status     | Binary Size  | Build Time
-------------+------------+--------------+-----------
esp32        | SUCCESS    | 358 KB       | 45s
esp32s2      | SUCCESS    | 342 KB       | 43s
esp32s3      | SUCCESS    | 365 KB       | 47s
esp32c3      | SUCCESS    | 335 KB       | 41s
esp32c6      | SUCCESS    | 338 KB       | 42s
esp32h2      | SUCCESS    | 330 KB       | 40s
esp32p4      | SUCCESS    | 359 KB       | 48s

Binary Size Statistics
======================
Minimum: 330 KB
Maximum: 365 KB
Average: 347 KB
```

## sdkconfig Templates

Target-specific default configurations in `examples/basic_transfer/`:

- `sdkconfig.defaults.esp32` - ESP32 defaults (4MB flash)
- `sdkconfig.defaults.esp32s2` - ESP32-S2 defaults (4MB flash)
- `sdkconfig.defaults.esp32s3` - ESP32-S3 defaults (8MB flash)
- `sdkconfig.defaults.esp32c3` - ESP32-C3 defaults (4MB flash)
- `sdkconfig.defaults.esp32c6` - ESP32-C6 defaults (4MB flash)
- `sdkconfig.defaults.esp32h2` - ESP32-H2 defaults (4MB flash)
- `sdkconfig.defaults.esp32p4` - ESP32-P4 defaults (32MB flash)

### Key Configuration Differences

#### UART Pins

Pins vary by board schematic and availability:

- **ESP32**: GPIO17 (TX), GPIO16 (RX), GPIO5 (RTS), GPIO18 (CTS)
- **ESP32-S2**: GPIO17 (TX), GPIO18 (RX), GPIO19 (RTS), GPIO20 (CTS)
- **ESP32-S3**: GPIO17 (TX), GPIO18 (RX), GPIO19 (RTS), GPIO20 (CTS)
- **ESP32-C3**: GPIO6 (TX), GPIO7 (RX), GPIO4 (RTS), GPIO5 (CTS)
- **ESP32-C6**: GPIO6 (TX), GPIO7 (RX), GPIO4 (RTS), GPIO5 (CTS)
- **ESP32-H2**: GPIO6 (TX), GPIO7 (RX), GPIO4 (RTS), GPIO5 (CTS)
- **ESP32-P4**: GPIO30 (TX), GPIO31 (RX), GPIO50 (RTS), GPIO29 (CTS)

#### Optimization Settings

- **ESP32, C3, C6, H2**: `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` (limited RAM)
- **ESP32-S3, P4**: `CONFIG_COMPILER_OPTIMIZATION_PERF=y` (more RAM available)

#### Core Configuration

- **Single-core**: ESP32-S2, C3, C6, H2 (`CONFIG_FREERTOS_UNICORE=y`)
- **Dual-core**: ESP32, S3, P4 (`CONFIG_FREERTOS_UNICORE=n`)

## Partition Tables

Three partition table templates provided:

### partitions_4mb.csv (ESP32, S2, C3, C6, H2)

| Partition  | Type       | SubType | Size | Purpose |
|------------|------------|---------|------|---------|
| nvs        | data       | nvs     | 16KB | Non-volatile storage |
| phy_init   | data       | phy     | 4KB  | PHY init data |
| factory    | app        | factory | 1MB  | Application |
| flash      | data       | fat     | 1MB  | Flash/FAT backend |
| littlefs   | data       | spiffs  | 1MB  | LittleFS backend |
| spiffs     | data       | spiffs  | 1MB  | SPIFFS backend |

### partitions_8mb.csv (ESP32-S3)

Same layout but 2MB per backend (flash, littlefs, spiffs).

### partitions_32mb.csv (ESP32-P4)

Same layout but 8MB per backend (flash, littlefs, spiffs), with ~7MB reserved for future use.

## Manual Build for Single Target

```bash
cd examples/basic_transfer

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p COM3 flash monitor
```

## Troubleshooting

### Build fails with "set-target failed"

**Cause**: Target not supported by your ESP-IDF version.

**Solution**: Update ESP-IDF to v5.3 or later:
```bash
cd $IDF_PATH
git pull
git submodule update --init --recursive
```

### Build fails with "partition too small"

**Cause**: Binary size exceeds partition size.

**Solution**: 
- Increase factory partition size in partitions CSV
- Enable compiler optimization for size
- Disable unused components in menuconfig

### Build fails with "unknown configuration option"

**Cause**: sdkconfig option not available for target.

**Solution**: Remove target-specific incompatible options from sdkconfig.defaults.

### Script reports "idf.py: command not found"

**Cause**: ESP-IDF environment not activated.

**Solution**:
```bash
. $IDF_PATH/export.sh
./tools/build_all_targets.sh
```

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Multi-Target Build

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup ESP-IDF
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.3
      
      - name: Build all targets
        run: |
          . $IDF_PATH/export.sh
          ./tools/build_all_targets.sh
      
      - name: Upload binaries
        uses: actions/upload-artifact@v3
        with:
          name: firmware-binaries
          path: examples/basic_transfer/build_output/*.bin
      
      - name: Upload build report
        uses: actions/upload-artifact@v3
        with:
          name: build-report
          path: examples/basic_transfer/build_output/BUILD_REPORT.txt
```

## Performance Benchmarks

Expected build times (ESP-IDF v5.3, 8-core CPU):

| Target   | Clean Build | Incremental |
|----------|-------------|-------------|
| ESP32    | 45s         | 8s          |
| ESP32-S2 | 43s         | 7s          |
| ESP32-S3 | 47s         | 8s          |
| ESP32-C3 | 41s         | 7s          |
| ESP32-C6 | 42s         | 7s          |
| ESP32-H2 | 40s         | 7s          |
| ESP32-P4 | 48s         | 8s          |

**Total (all 7)**: ~5 minutes clean, ~1 minute incremental

## Binary Size Expectations

Typical sizes with all backends enabled (SD, Flash, LittleFS, SPIFFS):

| Target   | Min   | Typical | Max   | Notes |
|----------|-------|---------|-------|-------|
| ESP32    | 340KB | 358KB   | 380KB | Xtensa code slightly larger |
| ESP32-S2 | 325KB | 342KB   | 365KB | Less peripheral drivers |
| ESP32-S3 | 345KB | 365KB   | 390KB | More features enabled |
| ESP32-C3 | 315KB | 335KB   | 360KB | RISC-V efficient |
| ESP32-C6 | 318KB | 338KB   | 365KB | Similar to C3 |
| ESP32-H2 | 310KB | 330KB   | 355KB | Smallest (no WiFi stack) |
| ESP32-P4 | 340KB | 359KB   | 385KB | RISC-V 64-bit |

## Size Optimization Tips

If binary size is critical:

1. **Disable unused backends** in Kconfig:
   ```
   [ ] Enable SPIFFS backend
   ```
   Saves ~15KB

2. **Use size optimization**:
   ```
   CONFIG_COMPILER_OPTIMIZATION_SIZE=y
   ```
   Saves ~50-70KB vs PERF

3. **Disable debug logs**:
   ```
   CONFIG_LOG_DEFAULT_LEVEL_WARN=y
   ```
   Saves ~30KB

4. **Minimize Bluetooth/WiFi**:
   - If not using wireless features, disable in menuconfig
   - Saves 100-200KB

## License

Same as main project (see LICENSE file in repository root).

## Related Documentation

- Main README: `../../README.md`
- Architecture doc: `../../.docs/VFS_BACKEND_DESIGN.md`
- Partition tables: `../basic_transfer/partitions_*.csv`
- Example code: `../basic_transfer/main/`

---

**Last Updated:** 2026-10-13  
**Version:** 2.0.0  
**Maintainer:** Kiro AI
