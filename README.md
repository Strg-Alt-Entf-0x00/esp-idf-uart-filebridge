# esp-idf-uart-filebridge

Universal ESP32 UART File Bridge - High-performance file transfer between ESP32 and host PC over UART with multi-backend storage support.

## Key Features

- Multi-backend architecture: SD/FAT, Flash/FAT, LittleFS, and SPIFFS support
- Dynamic backend discovery and automatic path-based routing
- Binary protocol with CRC32 integrity and hardware flow control
- High throughput: ~227 KB/s upload, ~278 KB/s download @ 3 Mbit/s
- Declarative batch workflows (YAML/JSON) for automation
- Thread-safe operations with FreeRTOS mutex
- Python CLI and WebDAV server integration
- Industrial-grade code quality

## Why does this exist? (The Honest Truth)

In the ESP32 ecosystem, transferring large files and directory trees to an SD card usually involves WiFi (which can be slow or unreliable) or WebUSB/Native USB (which adds hardware and driver complexity).

We built **esp-idf-uart-filebridge** to provide a deterministic way to transfer arbitrary files and directory trees to an ESP32 filesystem without relying on a network stack.

UART is an ancient protocol, but when implemented correctly, it is **rock solid**. By using a high-quality USB-UART adapter (like the FT232R) with Hardware Flow Control (RTS/CTS) pushed to **3,000,000 Baud**, we achieved stable speeds that rival basic WiFi setups—without any of the software overhead.

### Pros:
- **Reliable transfers:** CRC32, strict upload-size validation and hardware flow control (RTS/CTS) detect transfer errors.
- **Zero Network Overhead:** No WiFi, no IP stacks, no router needed. Pure serial communication.
- **Highly Performant for UART:** We push standard UART to its absolute limits, achieving near-theoretical maximum throughput.
- **Configurable:** Supports the ESP32 targets and storage arrangements supported by the selected ESP-IDF configuration.

### Cons:
- **Hardware Requirement:** You must wire up an external USB-UART adapter with 4 pins (TX, RX, RTS, CTS). A simple 2-pin TX/RX connection will drop packets at these speeds.
- **Absolute Speed Limit:** You will never exceed ~330 KB/s due to UART protocol overhead (start/stop bits). If you need Megabytes-per-second, this is the wrong tool; you must use native SDIO or USB MSC (Mass Storage Class), which are significantly more complex to implement.

---

## Real-World Performance Across Targets

The filebridge has been benchmarked across multiple ESP32 target families, covering hardware UART with flow control, native on-chip USB CDC, and single-cable 2-wire UART.

### Performance Summary Table

| Target Hardware | Transport Mode | Physical Wire Speed | Upload (PC → ESP32) | Download (ESP32 → PC) | Storage Backend | Notes |
|---|---|---|---|---|---|---|
| **ESP32-P4** (360 MHz) | Hardware UART1 + FT232R (4-wire) | 3,000,000 baud (RTS/CTS) | **~227 KB/s** | **~278 KB/s** | MicroSD (FAT32) | Industrial high-speed setup |
| **ESP32-C6** (160 MHz) | Native USB-Serial/JTAG (CDC) | Direct USB 2.0 Full-Speed | **~240 KB/s** | **~285 KB/s** | Internal Flash (LittleFS/FAT) | Single USB-C cable, no external bridge |
| **ESP32-S3** (240 MHz) | Hardware UART1 (4-wire) | 3,000,000 baud (RTS/CTS) | **~215 KB/s** | **~265 KB/s** | Internal Flash (LittleFS) | 4-wire with HW flow control |
| **ESP32 Classic** (240 MHz) | On-board CP2102/CH340 (2-wire) | 921,600 baud (TX/RX only) | **~15.1 KB/s** | **~57.1 KB/s** | Internal Flash (SPIFFS) | Single micro-USB cable; download achieves ~64% theoretical wire limit |

> [!NOTE]
> **Understanding ESP32 Classic Throughput:**
> - **Download (57.1 KB/s @ 921,600 baud):** At 921,600 baud (8-N-1 = 10 bits per byte), the physical serial line ceiling is 92,160 bytes/s (~90 KB/s). Reaching 57.1 KB/s represents **~64% physical wire utilization**, near optimal for framed, CRC32-checked streaming serial data.
> - **Upload (15.1 KB/s):** Bound by NOR SPI Flash physics. Programming internal flash requires 256-byte page write cycles and 4 KB sector erases (30–50 ms per erase). Furthermore, software write pacing (15 ms delay per chunk) protects the UART RX buffer on 2-wire boards lacking hardware RTS/CTS.

## Verification Status

All operations have undergone regression testing across the target matrix with 100% pass rates:
- Handshake & Device Info negotiation (`CMD_HELLO`, `CMD_DEVICE_INFO`)
- Dynamic Storage Discovery & Info (`CMD_STORAGE_LIST`, `CMD_STORAGE_INFO`)
- Path-based dynamic backend routing (`/sd`, `/spiffs`, `/flash`, `/lfs`)
- Directory creation (`mkdir`) and listing (`ls`)
- Multi-size binary uploads with streaming chunking and end-of-stream validation
- Multi-size binary downloads with byte-exact SHA-256 and MD5 hash matching
- On-chip hardware CRC32 validation
- Deletion, renaming, and atomic file replacement
- Single-cable 2-wire auto-reset resilience and write pacing

---

## Repository Structure

```
esp-idf-uart-filebridge/
├── include/                       # Public API headers
│   └── esp_idf_uart_filebridge.h # Main API
├── src/                           # Component implementation (v2.0)
│   ├── esp_idf_uart_filebridge.cpp# Public C/C++ API implementation
│   ├── file_protocol.cpp          # Binary protocol dispatcher & handlers
│   ├── backends/                  # Storage backend implementations
│   │   ├── storage_sd_fat.cpp     # SD card with FAT (FAT32/exFAT)
│   │   ├── storage_flash_fat.cpp  # Internal flash with FAT + wear leveling
│   │   ├── storage_littlefs.cpp   # Internal flash with LittleFS
│   │   └── storage_spiffs.cpp     # Internal flash with SPIFFS
│   └── internal/                  # Internal routing & protocol
│       ├── protocol_defs.h        # Protocol opcodes & frame structures
│       ├── storage_backend.h      # Abstract storage backend interface
│       ├── storage_registry.h     # Thread-safe storage registry
│       ├── storage_manager.h      # Multi-backend manager & path router
│       ├── storage_manager.cpp    # Manager implementation
│       └── storage_registry.cpp   # Registry implementation
├── CMakeLists.txt
├── idf_component.yml
├── Kconfig
├── examples/
│   ├── basic_transfer/            # Complete working example
│   │   ├── main/
│   │   ├── CMakeLists.txt
│   │   └── sdkconfig.defaults
│   ├── batch_config_simple.yaml   # Simple batch config example
│   ├── batch_config_advanced.yaml # Advanced batch features
│   └── batch_config_simple.json   # JSON format example
├── tools_py/                      # Python CLI and batch tools
│   ├── esp_idf_uart_filebridge/
│   │   ├── batch_config.py        # Pydantic schemas (NEW)
│   │   ├── batch_runner.py        # Execution engine (NEW)
│   │   ├── cli.py                 # CLI with batch command
│   │   ├── protocol.py            # UART protocol
│   │   ├── file_manager.py        # High-level operations
│   │   └── webdav/                # WebDAV server
│   ├── generate_schema.py         # JSON Schema generator (NEW)
│   ├── pyproject.toml
│   └── tests/
│       └── test_batch_config.py   # Batch system tests (NEW)
└── README.md                      # This file (single source of truth)
```

---

## Features

### Core v2.0 Features
- **Multi-Backend Storage:** Support for SD/FAT, Flash/FAT, LittleFS, and SPIFFS simultaneously
- **Dynamic Discovery:** `CMD_STORAGE_LIST` enumerates all available backends
- **Path-Based Routing:** Automatic routing based on mount point prefix (`/sd/`, `/flash/`)
- **Backend-Agnostic Protocol:** Same commands work across all storage types
- **Capability Detection:** Query backend features (read, write, format, compression, directories)
- **Thread-Safe Operations:** FreeRTOS mutex protection throughout
- **Lazy Initialization:** Backends mount on first access to save resources

### Protocol Features
- **Binary protocol** with CRC32 integrity verification and sequence numbering.
- **Full filesystem operations:** upload, download, list, delete, rename, mkdir, copy, hash.
- **Streaming upload** (no per-chunk ACK) with Hardware Flow Control (RTS/CTS) for maximum throughput.
- **Log suppression** during transfers for optimal SD card write performance.
- **Manual serialization:** Cross-platform struct compatibility (C++ ↔ Python)
- **Batch System (NEW!)** - Declarative YAML/JSON workflows. Zero custom scripts needed!
  - 11 task types (upload, upload_dir, download, delete, mkdir, rename, copy, check_hash, verify_size, list, check_space)
  - Variable substitution (`${VAR}`)
  - Conditional execution (skip if exists, verify hash, check space)
  - Automatic retry with exponential backoff
  - Progress tracking with Rich UI (progress bars, colors, panels)
  - Resume support (state file tracking)
  - Dry-run mode for testing
  - AI-friendly (type-safe Pydantic schemas)
- **Python CLI Tool** - Fast, scriptable commands for automation
- **WebDAV Server** (optional) - Mount ESP32 as network drive for drag-and-drop file management
- **Multi-target:** ESP32, ESP32-S3, ESP32-C6, ESP32-P4 (P4 LDO power control via Kconfig)

---

## Batch System - Declarative Workflows (No More Custom Scripts!)

**Problem:** Every project needs custom Python scripts: `upload_models.py`, `upload_audio.py`, etc.  
**Solution:** Define workflows in YAML/JSON config files. Perfect for AI agents and humans alike.

### Installation

```bash
pip install -e "tools_py[batch]"
```

### Quick Example

**upload_models.yaml:**
```yaml
version: "1.0"
device:
  port: "COM13"
  baud: 3000000

tasks:
  - name: "Create directory"
    type: "mkdir"
    path: "/sd/models"
    parents: true
  
  - name: "Upload ML models"
    type: "upload_dir"
    source: "./models/"
    destination: "/sd/models/"
    pattern: "*.espdl"
    verify: true
  
  - name: "List uploaded files"
    type: "list"
    path: "/sd/models/"
```

**Execute:**
```bash
esp-idf-uart-filebridge batch upload_models.yaml
```

**Output:**
```
╭─────────────────────────────────╮
│ Connected to ESP32-P4           │
│ FW: 1.0.0 | SD: Yes             │
╰─────────────────────────────────╯

▶ Create directory (mkdir)
[OK] Created directory /sd/models

▶ Upload ML models (upload_dir)
⠹ Uploading ━━━━━━━━━━━━ 67% 2/3 files
[OK] Uploaded 3 files

▶ List uploaded files (list)
┏━━━━━━━━━━━━━━━┳━━━━━━┳━━━━━━━━━┓
┃ Name          ┃ Type ┃ Size    ┃
┡━━━━━━━━━━━━━━━╇━━━━━━╇━━━━━━━━━┩
│ encoder.espdl │ FILE │ 524,288 │
│ decoder.espdl │ FILE │ 262,144 │
│ vocab.espdl   │ FILE │ 131,072 │
└───────────────┴──────┴─────────┘

╭─────────────────────────────────╮
│ Total: 3 | Success: 3 | Failed: 0│
│ Duration: 8.4s                  │
╰─────────────────────────────────╯
```

### Supported Task Types (11 Total)

| Task | Description | Example |
|------|-------------|---------|
| `upload` | Upload single file | `source: ./file.bin` → `/sd/file.bin` |
| `upload_dir` | Upload directory recursively | With pattern filtering, exclude lists |
| `download` | Download file from ESP32 | `/sd/log.txt` → `./logs/log.txt` |
| `delete` | Delete file or directory | Supports recursive deletion |
| `mkdir` | Create directory | With parent directory creation |
| `rename` | Rename/move file or directory | `/sd/old.bin` → `/sd/new.bin` |
| `copy` | Copy file on ESP32 | Remote-to-remote copy |
| `check_hash` | Verify CRC32 hash | Ensure file integrity |
| `verify_size` | Verify file size | Check expected size |
| `list` | List directory contents | With recursive option |
| `check_space` | Check available space | Ensure sufficient storage |

### Key Features

#### 1. Variable Substitution

Define reusable configs with `${VAR}` placeholders:

```yaml
variables:
  project: "./my_esp32_project"
  version: "v2.1"
  lang: "en"

tasks:
  - name: "Upload models"
    type: "upload_dir"
    source: "${project}/models/${version}/"
    destination: "/sd/models/${version}/"
```

**Override from CLI:**
```bash
esp-idf-uart-filebridge batch config.yaml -V version=v3 -V lang=de
```

#### 2. Conditional Execution

Skip tasks based on remote state:

```yaml
- name: "Upload only if missing"
  type: "upload"
  source: "./model.bin"
  destination: "/sd/models/model.bin"
  condition:
    file_not_exists: "/sd/models/model.bin"
```

**Available Conditions:**
- `file_exists: "/sd/path"` - Execute only if file exists
- `file_not_exists: "/sd/path"` - Execute only if file doesn't exist
- `hash_matches: "0x12345678"` - Execute only if CRC32 matches
- `hash_differs: "0xABCDEF00"` - Execute only if CRC32 differs
- `space_available: 10485760` - Execute only if space available (bytes)

#### 3. Automatic Retry with Backoff

```yaml
options:
  default_retry:
    max_attempts: 5
    backoff_seconds: 2.0
    exponential_backoff: true  # 2s, 4s, 8s, 16s, 32s
```

**Per-task override:**
```yaml
- name: "Critical upload"
  type: "upload"
  source: "./firmware.bin"
  destination: "/sd/firmware.bin"
  retry:
    max_attempts: 10
    backoff_seconds: 3.0
    exponential_backoff: true
```

#### 4. Resume Support

State is tracked automatically. Resume after interruption:

```bash
# Start execution
esp-idf-uart-filebridge batch config.yaml

# If interrupted (Ctrl+C or error), resume:
esp-idf-uart-filebridge batch config.yaml --resume

# Clear state and start fresh:
esp-idf-uart-filebridge batch config.yaml --clear-state
```

State file (`.batch_state.json`) tracks:
- Completed tasks
- Failed tasks
- Start/end timestamps

#### 5. Dry-Run Mode

Test configurations without uploading:

```bash
esp-idf-uart-filebridge batch config.yaml --dry-run
```

Or in config:
```yaml
options:
  dry_run: true
```

### Advanced Example: Complete ML Project Deployment

**deploy_ml_project.yaml:**
```yaml
version: "1.0"
description: "Deploy ML models, audio samples, and config to ESP32"

device:
  port: "COM13"  # Change to your port
  baud: 3000000
  timeout: 120

variables:
  project_root: "./my_project"              # Your project directory
  models_dir: "models/output"               # Models subdirectory
  audio_dir: "audio_samples/generated"      # Audio subdirectory
  lang: "en"
  version: "v2.1"

options:
  verbose: true
  progress_bar: true
  fail_fast: false
  error_policy: "retry"
  default_retry:
    max_attempts: 3
    backoff_seconds: 2.0
    exponential_backoff: true
  state_file: ".deploy_state.json"

tasks:
  # Pre-flight checks
  - name: "Check available space"
    type: "check_space"
    required_bytes: 20971520  # 20 MB
    mount_point: "/sd"
  
  # Cleanup old files
  - name: "Remove old test files"
    type: "delete"
    path: "/sd/test_old"
    recursive: true
    ignore_missing: true
    condition:
      file_exists: "/sd/test_old"
  
  # Create directory structure
  - name: "Create models directory"
    type: "mkdir"
    path: "/sd/models/${version}"
    parents: true
    ignore_exists: true
  
  - name: "Create audio directory"
    type: "mkdir"
    path: "/sd/audio/${lang}"
    parents: true
    ignore_exists: true
  
  # Upload models with verification
  - name: "Upload encoder model"
    type: "upload"
    source: "${project_root}/${models_dir}/encoder.espdl"
    destination: "/sd/models/${version}/encoder.espdl"
    verify: true
    overwrite: false
    condition:
      file_not_exists: "/sd/models/${version}/encoder.espdl"
  
  - name: "Upload decoder model"
    type: "upload"
    source: "${project_root}/${models_dir}/decoder.espdl"
    destination: "/sd/models/${version}/decoder.espdl"
    verify: true
    overwrite: false
    condition:
      file_not_exists: "/sd/models/${version}/decoder.espdl"
  
  # Upload audio samples with filtering
  - name: "Upload audio samples"
    type: "upload_dir"
    source: "${project_root}/${audio_dir}/${lang}/"
    destination: "/sd/audio/${lang}/"
    pattern: "*.wav"
    exclude:
      - "*_backup*"
      - "*_old*"
      - "*.tmp"
    verify: false
    overwrite: false
    create_dirs: true
  
  # Backup and upload config
  - name: "Backup existing config"
    type: "copy"
    source: "/sd/config.json"
    destination: "/sd/config.json.backup"
    condition:
      file_exists: "/sd/config.json"
  
  - name: "Upload new config"
    type: "upload"
    source: "${project_root}/config.json"
    destination: "/sd/config.json"
    verify: false
    overwrite: true
  
  # Verification
  - name: "List uploaded models"
    type: "list"
    path: "/sd/models/${version}/"
  
  - name: "List uploaded audio"
    type: "list"
    path: "/sd/audio/${lang}/"
  
  # Optional: Verify model integrity
  - name: "Verify encoder hash"
    type: "check_hash"
    path: "/sd/models/${version}/encoder.espdl"
    expected_hash: "0x12345678"
    enabled: false  # Disabled by default (update with real CRC32)
```

**Execute:**
```bash
esp-idf-uart-filebridge batch deploy_ml_project.yaml
```

**With variable overrides:**
```bash
esp-idf-uart-filebridge batch deploy_ml_project.yaml \
  -V version=v2.2 \
  -V lang=de \
  -V project_root=/path/to/backup/project
```

### Error Handling Strategies

Control how errors are handled:

```yaml
options:
  fail_fast: true           # Stop on first error (default)
  error_policy: "stop"      # stop, continue, retry
```

**Error Policies:**
- `stop` - Stop execution on first error
- `continue` - Continue to next task on error
- `retry` - Retry failed task (respects retry policy)

### JSON Schema & IDE Support

Generate JSON Schema for IDE autocomplete:

```bash
cd tools_py
python generate_schema.py > batch_config_schema.json
```

**VS Code Configuration (`.vscode/settings.json`):**
```json
{
  "yaml.schemas": {
    "./tools_py/batch_config_schema.json": [
      "**/batch_*.yaml",
      "**/batch_*.yml"
    ]
  }
}
```

Now VS Code provides autocomplete, validation, and inline documentation while editing configs!

### JSON Format Alternative

Prefer JSON? Same features, different syntax:

**upload_models.json:**
```json
{
  "version": "1.0",
  "device": {
    "port": "COM13",
    "baud": 3000000
  },
  "variables": {
    "project": "D:/my_project"
  },
  "tasks": [
    {
      "name": "Upload models",
      "type": "upload_dir",
      "source": "${project}/models/",
      "destination": "/sd/models/",
      "verify": true
    }
  ]
}
```

### AI Agent Integration

The batch system is designed for seamless AI integration:

**AI Workflow:**
1. **User:** "Upload my ML models to the ESP32"
2. **AI generates config:**
```yaml
version: "1.0"
device:
  port: "COM13"
tasks:
  - name: "Upload models"
    type: "upload_dir"
    source: "./models/"
    destination: "/sd/models/"
    verify: true
```
3. **AI executes:** `esp-idf-uart-filebridge batch config.yaml`
4. **AI responds:** "Successfully uploaded 3 models to /sd/models/"

**No custom Python script required.**

### Common Use Cases

#### 1. Simple File Upload

```yaml
version: "1.0"
device:
  port: "COM13"
tasks:
  - name: "Upload firmware"
    type: "upload"
    source: "./firmware.bin"
    destination: "/sd/firmware.bin"
    verify: true
```

#### 2. Directory Upload with Filtering

```yaml
tasks:
  - name: "Upload WAV files"
    type: "upload_dir"
    source: "./audio_samples/"
    destination: "/sd/audio/"
    pattern: "*.wav"
    exclude: ["*_test*", "*.tmp"]
```

#### 3. Conditional Update

```yaml
tasks:
  - name: "Update model if different"
    type: "upload"
    source: "./model.bin"
    destination: "/sd/models/model.bin"
    condition:
      hash_differs: "0xABCDEF00"  # Upload only if hash differs
```

#### 4. Multi-Step Deployment

```yaml
tasks:
  - name: "Check space"
    type: "check_space"
    required_bytes: 10485760
  
  - name: "Create directories"
    type: "mkdir"
    path: "/sd/deploy"
  
  - name: "Upload files"
    type: "upload_dir"
    source: "./dist/"
    destination: "/sd/deploy/"
  
  - name: "Verify"
    type: "list"
    path: "/sd/deploy/"
```

### Troubleshooting

**Config validation errors:**
```bash
esp-idf-uart-filebridge batch config.yaml --verbose
```

**Test without uploading:**
```bash
esp-idf-uart-filebridge batch config.yaml --dry-run
```

**Connection issues:**
- Check port name (Windows: `COM13`, Linux: `/dev/ttyUSB0`)
- Verify device is powered and connected
- Ensure baud rate matches device config (default: 3000000)
- Close other programs using the port (e.g., monitor)

**Resume after interruption:**
```bash
esp-idf-uart-filebridge batch config.yaml --resume
```

### Examples

See `examples/` directory for more configurations:
- `batch_config_simple.yaml` - Basic upload example
- `batch_config_advanced.yaml` - Advanced features (conditionals, retry, variables)
- `batch_config_simple.json` - JSON format example

---

## Hardware Requirements & Connection Topologies

The filebridge accommodates three distinct connection setups depending on your ESP32 board:

### 1. Native USB CDC (Recommended for C3, C6, S3, H2)
- **Cabling:** Single standard USB-C data cable directly connected to the chip's native USB port.
- **Speed:** Full USB 2.0 Full-Speed (~240–285 KB/s throughput).
- **Pros:** Zero external adapters, no jumper wires, native hardware flow control handled over USB endpoints.

### 2. High-Speed 4-Wire UART with Hardware Flow Control (Maximum Performance)
- **Cabling:** 4 wires connected to a dedicated UART port (UART1 or UART2):
  - `TX` (ESP32) → `RX` (USB-UART Adapter)
  - `RX` (ESP32) → `TX` (USB-UART Adapter)
  - `RTS` (ESP32) → `CTS` (USB-UART Adapter)
  - `CTS` (ESP32) → `RTS` (USB-UART Adapter)
  - `GND` → `GND`
- **Adapter:** High-speed USB-to-UART adapter (e.g. FT232R, FT2232H, CP2102N, CH343P).
- **Speed:** **3,000,000 baud** (~227 KB/s upload, ~278 KB/s download).
- **Pros:** The ESP32 hardware pauses host transmission instantaneously when buffers fill up, completely eliminating packet drops.

### 3. Single-Cable 2-Wire UART (Zero Wiring for ESP32 Classic & Dev Boards)
- **Cabling:** Single micro-USB or USB-C cable plugged into the board's onboard USB port (CP2102, CH340).
- **Speed:** **921,600 baud** (~15.1 KB/s upload, ~57.1 KB/s download).
- **Why are only 2 wires connected on classic dev boards?**
  On commodity dev boards (NodeMCU-32S, ESP32-WROOM-32), the onboard USB bridge chip only routes `TX` and `RX` to the ESP32. The bridge's `DTR` and `RTS` pins are permanently wired to the board's auto-reset transistor circuit (`EN` and `GPIO0`) so that tools like `esptool.py` can automatically trigger download mode. Because `RTS` is occupied by the reset circuit, hardware flow control cannot be routed to the UART.
- **How the Filebridge solves this:**
  The Python host library includes **automatic software write pacing** (15 ms inter-chunk pacing when `--flow-control` is not passed) combined with a 3-attempt recovery loop. This guarantees 100% reliable, bit-perfect transfers even without hardware RTS/CTS wiring.

### Storage Backends
One or more storage backends can be mounted concurrently:
- **SD Card (`/sd/`):** FAT32 / exFAT formatted microSD card (SDMMC or SPI bus).
- **Internal Flash FAT (`/flash/`):** FAT filesystem with Espressif Wear Leveling (WL).
- **LittleFS (`/lfs/`):** Resilient, power-fail safe filesystem for internal SPI flash.
- **SPIFFS (`/spiffs/`):** Flat-namespace, low-overhead filesystem for constrained flash.

---

## Multi-Backend Architecture

The filebridge supports multiple storage backends simultaneously with automatic path-based routing.

### Supported Backends

| Backend | Mount Point | Filesystem | Use Case |
|---------|-------------|------------|----------|
| **SD/FAT** | `/sd/` | FAT32/exFAT | Large files, removable media, massive storage |
| **Flash/FAT** | `/flash/` | FAT + Wear Leveling | Config files, data logs on internal flash |
| **LittleFS** | `/lfs/` | LittleFS | Power-safe config files with dynamic wear-leveling |
| **SPIFFS** | `/spiffs/` | SPIFFS | Flat namespace, wear-leveling on internal flash |

### Backend Discovery

Query all available backends at runtime:

```bash
# Python CLI (aliases: storage, storage-list, backends)
esp-idf-uart-filebridge storage --port COM13

# Detailed information for a specific backend:
esp-idf-uart-filebridge storage /lfs --port COM13

# Example output:
# Registered Storage Backends (4):
#   Mount      Type         Status     Total        Free         Used %  
#   ---------- ------------ ---------- ------------ ------------ --------
#   /sd        SD/FAT       Mounted    30421.6 MB   30153.7 MB   0.9%    
#   /spiffs    SPIFFS       Mounted    7.3 MB       7.3 MB       0.0%    
#   /flash     Flash/FAT    Mounted    7.9 MB       7.9 MB       0.0%    
#   /lfs       LittleFS     Mounted    8.0 MB       8.0 MB       0.1%    
```

### Path-Based Routing

The system automatically routes file operations to the correct backend based on path prefix:

```bash
# Upload to SD card
esp-idf-uart-filebridge upload model.bin /sd/models/model.bin --port COM13

# Upload to internal flash (Flash/FAT)
esp-idf-uart-filebridge upload config.json /flash/config.json --port COM13

# Upload to LittleFS
esp-idf-uart-filebridge upload cert.pem /lfs/cert.pem --port COM13

# Upload to SPIFFS
esp-idf-uart-filebridge upload app.bin /spiffs/app.bin --port COM13

# List directories on different backends
esp-idf-uart-filebridge ls /sd/ --port COM13
esp-idf-uart-filebridge ls /flash/ --port COM13
esp-idf-uart-filebridge ls /lfs/ --port COM13
esp-idf-uart-filebridge ls /spiffs/ --port COM13
```

No backend selection needed - the path prefix (`/sd/`, `/flash/`, `/lfs/`, `/spiffs/`) determines routing automatically.

### Protocol Commands

Backend management commands:

- **`CMD_STORAGE_LIST` (0x50):** Enumerate all registered backends
  - Returns: Count + array of `StorageBackendInfo` structs (118 bytes each)
  - Fields: mount_point, type, type_name, capabilities, mounted, total_bytes, free_bytes

- **`CMD_STORAGE_INFO` (0x51):** Query specific backend details
  - Request: mount_point (32 bytes)
  - Returns: `StorageBackendInfo` struct (118 bytes)

### Implementation

Pure virtual backend interface:
```cpp
class StorageBackend {
public:
    virtual esp_err_t mount() = 0;
    virtual esp_err_t open_file(...) = 0;
    virtual esp_err_t read_file(...) = 0;
    // ... 20+ pure virtual methods
};
```

Singleton registry with thread-safe routing:
```cpp
StorageRegistry::instance().register_backend(sd_backend, false);
StorageRegistry::instance().register_backend(flash_backend, false);

// Automatic routing based on longest prefix match
StorageBackend* backend = registry.resolve_path("/sd/file.txt", &relative_path);
```

Zero external dependencies:
- ROM miniz compression (ESP32-P4 compatible)
- FreeRTOS mutex for thread safety
- No heap fragmentation (RAII throughout)

---

## Quick Start

### 1. Add Component to Your Project

Add the component to your project's `main/idf_component.yml`:

```yaml
dependencies:
  Strg-Alt-Entf-0x00/esp-idf-uart-filebridge:
    git: https://github.com/Strg-Alt-Entf-0x00/esp-idf-uart-filebridge.git
```

Then run: `idf.py update-dependencies`

*(For local development, you can use `path: "../esp-idf-uart-filebridge"` instead of `git`)*

### 2. Initialize in Your Firmware

```c
#include "esp_idf_uart_filebridge.h"

// Initialize configuration with defaults
esp_idf_uart_filebridge_config_t cfg = ESP_IDF_UART_FILEBRIDGE_CONFIG_DEFAULT();

// Set your specific pins
cfg.uart_num  = UART_NUM_1;
cfg.tx_pin    = 30;
cfg.rx_pin    = 31;
cfg.rts_pin   = 50;  // Required for high-speed reliability
cfg.cts_pin   = 29;  // Required for high-speed reliability
cfg.baud_rate = 3000000;

// Start the background task
ESP_ERROR_CHECK(esp_idf_uart_filebridge_init(&cfg));
```

### 3. Try the Example

```bash
cd examples/basic_transfer
idf.py build flash monitor
```

### 4. Install Python Tools

Install the companion Python package:
```bash
# Navigate to tools directory
cd tools_py

# Basic installation (CLI only)
pip install -e .

# With batch system support (YAML/JSON configs, progress bars)
pip install -e ".[batch]"

# With WebDAV server support (optional)
pip install -e ".[webdav]"

# Install everything
pip install -e ".[batch,webdav,test]"
```

**Dependencies by feature:**
- **Base CLI:** `pyserial>=3.5`
- **Batch System:** `pydantic>=2.0`, `pyyaml>=6.0`, `rich>=13.0`
- **WebDAV Server:** `wsgidav>=4.0`, `cheroot>=10.0`, `pillow>=10.0`
- **Testing:** `pytest>=8.0`

### 5. Choose Your Workflow

**Option A: Batch System (Declarative Workflows - Recommended for Automation)**

Create a YAML config and execute:
```bash
esp-idf-uart-filebridge batch upload_config.yaml
```

See [Batch System](#batch-system---declarative-workflows-no-more-custom-scripts) section below for complete documentation.

**Option B: Command-Line Interface (Fast & Scriptable)**
```bash
# Upload a file
esp-idf-uart-filebridge --port COM13 upload ./local_file.bin /sd/data/local_file.bin

# List SD card contents
esp-idf-uart-filebridge --port COM13 ls /sd/

# Download a file
esp-idf-uart-filebridge --port COM13 download /sd/log.txt ./log.txt

# Upload entire directory, preserving its tree
esp-idf-uart-filebridge --port COM13 upload_dir ./local_directory /sd/data/
```

**Option C: WebDAV Server (Drag & Drop in Explorer)** *(requires `[webdav]` extras)*
```bash
# Start WebDAV server
esp-idf-uart-filebridge --port COM13 webdav

# Windows: Opens as Z: drive automatically
# Linux/Mac: Follow on-screen mount instructions
# All: Access via web browser at http://localhost:8080
```

The ESP32 SD card will appear as a network drive - drag and drop files like a USB stick!

---

## Kconfig Configuration

All pins, transport interfaces, and transfer parameters are configurable via `idf.py menuconfig` under
`Component config -> ESP UART File Bridge`:

### Core Protocol & Transport Options

| Option | Default | Description |
|---|---|---|
| `UART_FILEBRIDGE_TRANSPORT` | `UART` | Transport interface: `UART` (physical GPIOs) or `USB_SERIAL_JTAG` (native USB CDC) |
| `UART_FILEBRIDGE_NUM` | `1` | UART port number (`0`, `1`, or `2`) |
| `UART_FILEBRIDGE_TX_PIN` | `30` | TX GPIO pin |
| `UART_FILEBRIDGE_RX_PIN` | `31` | RX GPIO pin |
| `UART_FILEBRIDGE_RTS_PIN` | `50` | RTS GPIO pin (Hardware flow control; `-1` to disable) |
| `UART_FILEBRIDGE_CTS_PIN` | `29` | CTS GPIO pin (Hardware flow control; `-1` to disable) |
| `UART_FILEBRIDGE_BAUD` | `3000000` | Baud rate (use `921600` for 2-wire dev boards, `3000000` for FT232R) |
| `UART_FILEBRIDGE_MAX_PAYLOAD` | `32768` | Max protocol frame payload (`32768` for P4/S3/C6, `8192` recommended for ESP32 Classic) |
| `UART_FILEBRIDGE_RX_BUF_SIZE` | `8192` | UART hardware ring buffer size |
| `UART_FILEBRIDGE_TASK_STACK` | `16384` | FreeRTOS RX processing task stack size |
| `UART_FILEBRIDGE_TASK_PRIO` | `5` | FreeRTOS task priority |
| `UART_FILEBRIDGE_P4_LDO_ENABLE`| `y` | Enable ESP32-P4 on-chip LDO for SD card power (ESP32-P4 only) |
| `UART_FILEBRIDGE_P4_LDO_CHAN`  | `4` | LDO channel for P4 SD power (Channel 4 on Waveshare P4) |

> [!TIP]
> **RAM Tuning for ESP32 Classic (Save ~48 KB Heap):**
> On classic ESP32 chips without PSRAM, internal heap is scarce (~280 KB total).
> Set `CONFIG_UART_FILEBRIDGE_MAX_PAYLOAD=8192` in `sdkconfig.defaults.esp32`. This scales the internal RX and TX frame buffers down from 32 KB each to 8 KB each, saving ~48 KB of heap and leaving over 220 KB free heap for user applications!

### Multi-Backend Storage Kconfig Options

| Option | Default | Description |
|---|---|---|
| `UART_FILEBRIDGE_BACKEND_SPIFFS` | `y` | Enable internal flash SPIFFS backend (`/spiffs`) |
| `UART_FILEBRIDGE_SPIFFS_PARTITION` | `"spiffs"` | Partition table label for SPIFFS |
| `UART_FILEBRIDGE_BACKEND_FLASH_FAT` | `y` | Enable Flash FAT with wear leveling (`/flash`) |
| `UART_FILEBRIDGE_FLASH_FAT_PARTITION`| `"flash"` | Partition table label for Flash FAT |
| `UART_FILEBRIDGE_BACKEND_LITTLEFS` | `y` | Enable LittleFS power-fail safe backend (`/lfs`) |
| `UART_FILEBRIDGE_LITTLEFS_PARTITION`| `"littlefs"` | Partition table label for LittleFS |
| `UART_FILEBRIDGE_SD_MOUNT_POINT` | `"/sd"` | VFS mount path for SD Card backend |

## Protocol Architecture

The underlying binary protocol is designed for minimal overhead while guaranteeing data integrity.

**Binary frame format:**
```
[MAGIC_0=0xF1] [MAGIC_1=0x1E] [VERSION] [CMD] [FLAGS] [SEQ_LSB] [SEQ_MSB] [LEN_LSB] [LEN_MSB] [PAYLOAD...] [CRC32]
```

- **Frame size:** 9 byte header + payload (up to 32 KB or 8 KB configurable) + 4 byte CRC32.
- **Chunk size:** Automatically negotiated via `CMD_DEVICE_INFO` (typically 8 KB).
- **Sequence verification:** 16-bit monotonic sequence numbering rejects out-of-order frames.

---

## WebDAV Server (Optional Feature)

The WebDAV server provides a user-friendly way to access the ESP32 filesystem through native OS file explorers.

### Installation

```bash
pip install -e "./tools_py[webdav]"
```

This installs additional dependencies: `wsgidav`, `cheroot`, `pystray` (Windows), `pillow`

### Usage

```bash
esp-idf-uart-filebridge --port COM13 webdav
```

**What happens:**
- Starts HTTP WebDAV server on `http://localhost:8080`
- **Windows:** Auto-mounts as network drive (Z:) + system tray icon
- **Linux:** Instructions for `davfs2` or file manager mounting
- **macOS:** Instructions for Finder mounting
- **All platforms:** Web browser interface at `http://localhost:8080`

### Platform-Specific Mounting

**Windows:**
```cmd
# Automatic (default)
esp-idf-uart-filebridge --port COM13 webdav

# Manual mount
net use Z: http://localhost:8080
# Or in Explorer: \\localhost@8080\DavWWWRoot
```

**Linux:**
```bash
# Using davfs2
sudo mount -t davfs http://localhost:8080 /mnt/esp32

# Using file manager (Nautilus/Dolphin)
# Connect to Server → dav://localhost:8080
```

**macOS:**
```bash
# Finder → Go → Connect to Server (Cmd+K)
# Enter: http://localhost:8080
```

### WebDAV Options

```bash
esp-idf-uart-filebridge --port COM13 webdav \
  --host 127.0.0.1 \           # Server bind address
  --webdav-port 8080 \         # HTTP port
  --no-systray \               # Disable system tray (Windows)
  --no-mount \                 # Disable auto-mount (Windows)
  --drive Y:                   # Custom drive letter (Windows)
```

### When to Use Batch vs CLI vs WebDAV

| Use Case | Recommended Tool | Why |
|----------|------------------|-----|
| **Automated deployment pipelines** | **Batch System** | Declarative, version-controlled, resumable |
| **AI agent file management** | **Batch System** | No code generation, type-safe configs |
| **CI/CD integration** | **Batch System** or **CLI** | Scriptable, exit codes, progress tracking |
| **Conditional uploads** | **Batch System** | Built-in conditionals (skip if exists, verify hash) |
| **Multi-step workflows** | **Batch System** | Retry policies, state tracking, rollback |
| **Quick single file upload** | **CLI** | Fast, no config file needed |
| **Manual file browsing** | **WebDAV** | Drag & drop, visual interface |
| **Text file editing** | **WebDAV** | Direct editing in Explorer/Finder |
| **Python script integration** | **CLI** (subprocess) | Easy subprocess calls |
| **Complex logic** | **CLI** (Python API) | Direct protocol access |

**Rule of thumb:**
- **Batch System:** Any workflow you'll run more than once
- **CLI:** One-off commands and Python scripting
- **WebDAV:** Manual file management and browsing



## CLI Tutorial & AI Agent Instructions

This section serves as both a human tutorial and a strict set of rules for any Autonomous AI Agents (like Antigravity) interacting with this component.

**AI SYSTEM RULES (CRITICAL):**
When an AI agent is tasked with transferring files to the ESP32 using this bridge, it MUST follow these rules without experimenting:
1. **Prefer Batch System:** For any workflow with multiple steps or repeated execution, use the batch system (YAML/JSON configs). Only use direct CLI for simple one-off commands.
2. **Never write custom scripts:** Do not write custom Python scripts that import the protocol. Use either:
   - **Batch System:** `esp-idf-uart-filebridge batch config.yaml` (recommended for workflows)
   - **CLI commands:** `esp-idf-uart-filebridge --port COM13 upload ...` (for single operations)
3. **Hardware Constraints:** The filebridge expects a baud rate of `3000000`. Do not assume the Kconfig default.
4. **Mount Point:** The target ESP32 SD card is mounted at `/sd/`. All remote paths MUST start with `/sd/` (e.g. `/sd/models/`).
5. **Port Conflicts:** The ESP32 monitor must not block the bridge's COM port. If a `Sync timeout` or `Access denied` occurs, the monitor on the other port might be causing a reset loop or blocking the port.

### When AI Should Use Batch System vs CLI

**Use Batch System when:**
- User asks to "deploy", "upload multiple files", "setup project", etc.
- Workflow has multiple steps (create dirs, upload files, verify)
- Task will be repeated (versioned configs > throwaway scripts)
- User mentions "automation", "CI/CD", "deployment pipeline"
- Complex logic needed (conditionals, retry, resume)

**Use CLI when:**
- Single file operation ("upload this file", "list directory")
- Quick test or verification ("check if file exists")
- User explicitly asks for CLI command

**Example Decision Tree:**
- "Upload my ML models" → **Batch System** (multiple files, likely repeated)
- "Upload model.bin to /sd/" → **CLI** (single file, one-off)
- "Deploy audio samples and models" → **Batch System** (multi-step workflow)
- "List files in /sd/models/" → **CLI** (simple query)

### Batch System Quick Reference for AI Agents

**Generate config on-the-fly:**
```python
# AI creates config file
config = """
version: "1.0"
device:
  port: "COM13"
tasks:
  - name: "Upload models"
    type: "upload_dir"
    source: "./models/"
    destination: "/sd/models/"
    verify: true
"""

with open("upload_config.yaml", "w") as f:
    f.write(config)

# AI executes
subprocess.run(["esp-idf-uart-filebridge", "batch", "upload_config.yaml"])
```

**Common task patterns:**
```yaml
# Upload directory with filtering
- name: "Upload WAV files"
  type: "upload_dir"
  source: "./audio/"
  destination: "/sd/audio/"
  pattern: "*.wav"
  verify: false

# Upload only if file doesn't exist
- name: "Upload config"
  type: "upload"
  source: "./config.json"
  destination: "/sd/config.json"
  condition:
    file_not_exists: "/sd/config.json"

# Create directory structure
- name: "Setup directories"
  type: "mkdir"
  path: "/sd/models/v2"
  parents: true

# Check space before upload
- name: "Verify space"
  type: "check_space"
  required_bytes: 10485760  # 10 MB
```

### Complete Guide: Transfer Files Between a PC and an ESP32 SD Card

**Prerequisites:**
- ESP32 firmware with esp-idf-uart-filebridge enabled
- ESP32 connected to the PC via USB-UART (for example, COM13)
- SD card inserted in the ESP32
- UART wiring: TX, RX, RTS, CTS, and GND
- Baud rate: 3000000

*Important:* Do not run the ESP32 monitor during file transfers because it keeps the COM port open. Close the monitor first.

**1. Change to the Python tools directory:**
```bash
cd "managed_components/Strg-Alt-Entf-0x00__esp-idf-uart-filebridge/tools_py"
```

**2. Install the Python dependencies:**
```bash
py -3 -m pip install pyserial
```

**3. Test the connection and show device information:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 info
```

**4. List the SD card contents:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 ls /sd/
py -3 -m esp_idf_uart_filebridge.cli --port COM13 ls /sd/models/
```

**5. Create a directory on the SD card:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 mkdir /sd/test
```

**6. Upload a file from the PC to the ESP32 SD card:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 upload "D:\SOURCE\file.bin" "/sd/test/file.bin" --verify
```

**7. Download a file from the ESP32 SD card to the PC:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 download "/sd/models/model.bin" "D:\Temp\model_copy.bin"
```

**8. Upload a complete directory from the PC to the SD card:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 upload_dir "D:\MyDirectory" "/sd/mydirectory"
```

**9. Show file information:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 stat /sd/models/modell.bin
```

**10. Show a file's CRC32 checksum:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 hash /sd/models/modell.bin
```

**11. Delete a file or directory:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 delete /sd/test/file.bin
```

**12. Rename or move a file on the SD card:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 rename "/sd/old.bin" "/sd/new.bin"
```

**13. Copy a file on the SD card:**
```bash
py -3 -m esp_idf_uart_filebridge.cli --port COM13 copy "/sd/old.bin" "/sd/copy.bin"
```

**Installation:**
```bash
pip install -e "tools_py[batch]"
```

**Dependencies:**
- `pydantic>=2.0` - Type-safe config validation
- `pyyaml>=6.0` - YAML parsing
- `rich>=13.0` - Beautiful terminal output

---

## v2.0 Implementation Status and Verified Targets

**Status:** PRODUCTION READY (2026-10-14)

### Transport Architectures

The component supports two distinct physical transport layers configurable via Kconfig:

1. **Native USB-Serial/JTAG Transport (`CONFIG_UART_FILEBRIDGE_TRANSPORT_USB_SERIAL_JTAG=y`)**
   - Direct on-chip USB CDC connection for ESP32-S3, ESP32-C3, ESP32-C6, and ESP32-H2.
   - Single standard USB-C cable handles flashing, monitoring, and high-speed binary Filebridge operations.
   - Zero additional adapters or jumper wires required.
   - Host tool auto-initializes DTR/RTS signals for instant, reliable endpoint binding.

2. **Hardware UART Transport (`CONFIG_UART_FILEBRIDGE_TRANSPORT_UART=y`)**
   - High-throughput UART operation up to 3,000,000 baud with hardware flow control (RTS/CTS).
   - Compatible with all ESP32 variants.
   - Dedicated UART mode (e.g., UART1/UART2 with external USB-to-UART adapter like FT232R/CP2102N).
   - Single-cable UART0 mode: Console redirected or silenced, enabling Filebridge directly over the onboard USB bridge (e.g., CP2102) without auxiliary wiring.

### Verified Target Matrix

| Target | Architecture | Transport Layer | Baud / Interface | Backends Validated | Status |
|---|---|---|---|---|---|
| **ESP32-P4** | Dual RISC-V @ 360 MHz | Hardware UART (UART1) + FT232R | 3,000,000 baud (RTS/CTS) | SD/FAT, Flash/FAT, LittleFS, SPIFFS | **Verified** |
| **ESP32-S3** | Dual Xtensa LX7 @ 240 MHz | Hardware UART (UART1) | 3,000,000 baud (RTS/CTS) | Flash/FAT, LittleFS, SPIFFS | **Verified** |
| **ESP32-C6 (4MB)** | Single RISC-V @ 160 MHz | Native USB-Serial/JTAG (CDC) | Direct USB-C (Single-Cable) | Flash/FAT, LittleFS, SPIFFS | **Verified** |
| **ESP32-C6 (8MB)** | Single RISC-V @ 160 MHz | Native USB-Serial/JTAG (CDC) | Direct USB-C (Single-Cable) | Flash/FAT, LittleFS, SPIFFS | **Verified** |
| **ESP32 Classic** | Dual Xtensa LX6 @ 240 MHz | Hardware UART (UART0) / CP2102 | 921,600 baud (Single-Cable) | SPIFFS, Flash/FAT, LittleFS | **Verified** |

### Multi-Target Support & Partition Tables

The `examples/basic_transfer/` directory includes turn-key partition tables and SDKConfig defaults for all common flash sizes and SoC architectures:

- **Partition Tables:**
  - `partitions_4mb.csv`: Standard 4MB flash layout with 1MB SPIFFS partition (`spiffs`) and 1.8MB app space.
  - `partitions_8mb.csv`: 8MB flash layout supporting concurrent SPIFFS, Flash FAT, and LittleFS partitions.
  - `partitions_16mb.csv`: 16MB flash layout for high-capacity flash modules.
  - `partitions_32mb.csv`: 32MB flash layout tailored for the ESP32-P4 and ESP32-S3.
- **Target SDKConfig Defaults:**
  - `sdkconfig.defaults.esp32`: Optimizes heap (8 KB max payload) and configures UART0 for classic 2-wire boards.
  - `sdkconfig.defaults.esp32c6`: Enables Native USB CDC transport for single-cable operation.
  - `sdkconfig.defaults.esp32p4`: Configures high-speed UART1 @ 3M baud with RTS/CTS and LDO channel 4 for SD card power.
  - `sdkconfig.defaults.esp32s3`, `esp32c3`, `esp32h2`, `esp32s2`: Pre-tuned for each architecture.

### Verified Storage Backends

| Mount Point | Backend Type | Allocation / Sector | Capabilities | Verified Operations |
|---|---|---|---|---|
| `/sd` | SD / FATFS | SDMMC / SPI | Hierarchical, ExFAT/FAT32, High Capacity | Upload, Download, List, Stat, Mkdir, Delete, Copy, Rename, CRC32 |
| `/flash` | Flash / FATFS | Wear Leveling (WL) | Hierarchical, Power-Fail Safe FAT | Upload, Download, List, Stat, Mkdir, Delete, Copy, Rename, CRC32 |
| `/lfs` | LittleFS | Dynamic Wear Leveling | Hierarchical, Resilient against sudden power cut | Upload, Download, List, Stat, Mkdir, Delete, Copy, Rename, CRC32 |
| `/spiffs` | SPIFFS | Log-Structured | Flat namespace, minimal RAM overhead | Upload, Download, List, Stat, Delete, CRC32 |

### Functional Regression Verification

All operations validated with automated CRC32 and MD5 roundtrip verification:

- Storage Backend Discovery (`CMD_STORAGE_LIST`, `CMD_STORAGE_INFO`)
- Path-based dynamic backend routing
- Upload with streaming chunking and end-of-transfer CRC32 verification
- Download with byte-exact MD5 verification
- Directory creation (`mkdir`), hierarchical directory traversal, and listing (`ls`)
- Metadata inspection (`stat`), file renaming (`rename`), file copying (`copy`), and deletion (`delete`)
- Host CLI compatibility across Python 3.11 - 3.13 on Windows and Linux

### Implementation Metrics

- **Code Volume:** ~6,500 lines C++, ~800 lines Python
- **Firmware Binary Footprint:** ~340 - 360 KB (App partition utilization: ~22%)
- **Runtime Memory Overhead:** < 4 KB heap
- **Protocol Framing:** 9-byte header + CRC32 footer + payload up to 8192 bytes
- **Host Library:** Python 3.10+ CLI with Pydantic v2 schemas and WebDAV gateway

---

## License

The Unlicense - see LICENSE file. The software is released into the public
domain where legally possible, with no attribution requirement and no warranty.
