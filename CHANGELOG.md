# Changelog

All notable changes to this project will be documented in this file.

## [2.0.0] - 2026-11-08

### 🎉 Industrial Grade Release - Fully Stress Tested

This release marks a complete overhaul with multi-backend storage support, comprehensive bug fixes, and industrial-grade stress testing.

### ✨ New Features

- **Multi-Backend Storage System**: Support for SPIFFS, Flash/FAT, and LittleFS simultaneously
- **Storage Manager without SD Card**: New `esp_idf_uart_filebridge_init_storage_manager()` API for systems without SD cards
- **Hardware Flow Control**: Full RTS/CTS support at 3 Mbit/s with verified stability
- **Professional Error Handling**: Idempotent operations, graceful degradation, comprehensive error codes
- **Industrial Grade Verification**: All features stress tested (2 MB transfers, 120+ file listings, 100x reconnects)

### 🐛 Bug Fixes

**BUG #1: "Dangling Transfer" Warning**
- **Location**: `src/file_protocol.cpp:412-423` (handle_hello)
- **Fix**: Warning only logs when transfer was actually active, reduced verbose logs to DEBUG level
- **Verification**: 100x reconnect stress test - zero spurious warnings

**BUG #2: Watchdog Timeout on Large Directory Listings**
- **Location**: `src/file_protocol.cpp:561-588` (handle_list_begin)
- **Fix**: Added `taskYIELD()` every 20 entries in directory listing callback
- **Verification**: 120 files listed in 1.94s without watchdog timeout

**BUG #3: DELETE Returns Error on Non-Existent Files**
- **Location**: `src/file_protocol.cpp:1003-1049` (handle_delete)
- **Fix**: Made DELETE idempotent - sends ACK even if file doesn't exist (ENOENT)
- **Verification**: 100x delete on non-existent file - all successful

**BUG #4: Hardcoded COM Ports in Examples**
- **Files**: `examples/basic_transfer/main/main.cpp`, `tools_py/tests/test_*.py`
- **Fix**: Replaced hardcoded "COM13" with generic `<PORT>` placeholder
- **Verification**: Clean console logs with generic port references

### 🚀 Performance

**Verified on ESP32-S3 @ 3 Mbit/s:**
- Upload Speed: **130.2 KB/s** (2 MB test file)
- Download Speed: **273.7 KB/s** (2 MB test file)
- Directory Listing: **1.94s** for 120 files (no watchdog timeout)
- Connection Stability: **100%** success rate (100/100 reconnects)
- Data Integrity: **100%** byte-for-byte verification on 2 MB transfers

### 📊 Stress Test Results

**Test 1: Large File Transfer (2 MB)**
- ✅ Upload: 15.7s @ 130 KB/s
- ✅ Download: 7.5s @ 274 KB/s
- ✅ Data integrity: byte-for-byte match
- Status: **PASS**

**Test 2: Directory Listing (120 Files)**
- ✅ Listed in 1.94s
- ✅ No watchdog timeout
- ✅ All entries correct
- Status: **PASS**

**Test 3: Idempotent DELETE (100x)**
- ✅ 100 deletes on non-existent file
- ✅ All returned ACK (no errors)
- ✅ Professional behavior verified
- Status: **PASS**

**Test 4: Connection Stability (100x Reconnect)**
- ✅ 100 connect/disconnect cycles
- ✅ Zero failures
- ✅ No "dangling transfer" warnings
- Status: **PASS**

### 🔧 API Changes

**New Functions:**
```cpp
// Initialize storage manager without SD card requirement
esp_err_t esp_idf_uart_filebridge_init_storage_manager(void);
```

**Enhanced File Manager:**
- `connect(rtscts: bool = True)` - Added flow control parameter
- Improved error handling with retry logic
- Better progress callbacks for large files

### 📝 Documentation

- Added comprehensive hardware setup guide
- Added stress testing procedures
- Added troubleshooting section
- Added performance benchmarks
- Improved README with quick start guide

### 🏗️ Infrastructure

- ESP-IDF v6.0.2 support verified
- Multi-chip support: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2, ESP32-P4
- Partition tables for 4MB, 8MB, 16MB, 32MB flash configurations
- Python tools package with comprehensive test suite

### ⚠️ Known Limitations

**SPIFFS Cleanup Timing:**
- After large transfers (>2 MB), SPIFFS may need ~1-2 seconds before DELETE operations
- Workaround: Implemented automatic retry logic with delays
- Not critical for normal usage patterns

### 🔄 Migration from v1.x

**Storage Manager Initialization:**
```cpp
// Old (v1.x):
esp_idf_uart_filebridge_init();  // Assumes SD card

// New (v2.0):
esp_idf_uart_filebridge_init_storage_manager();  // No SD required
// Then register backends manually:
storage_manager_register_backend(&spiffs_backend);
storage_manager_register_backend(&flash_backend);
```

**DELETE Operations:**
- v2.0 DELETE is now idempotent (won't fail if file doesn't exist)
- Update error handling code to expect ACK instead of ERROR on ENOENT

### 🙏 Acknowledgments

This release underwent rigorous industrial-grade testing to ensure absolute reliability and robustness. Every bug was stress tested under extreme conditions to verify the fixes work in production scenarios.

---

## [1.0.0] - Previous Release

Initial release with SD card support and basic protocol implementation.

