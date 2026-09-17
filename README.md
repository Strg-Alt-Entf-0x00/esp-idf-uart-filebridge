# esp-idf-uart-filebridge

Universal ESP32 UART File Bridge - A highly optimized IDF Component for reliable file transfer between an ESP32 and a host PC over UART.

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

## Real-World Performance

Measured on one ESP32-P4 setup (360 MHz) using an FT232R adapter at **3,000,000 baud** with Hardware Flow Control enabled.

| Scenario | File Size | Upload (PC -> SD) | Download (SD -> PC) |
|---|---|---|---|
| **Small File** (Overhead Test) | 1 KB | ~0.22s | ~0.22s |
| **Large File** (Throughput Test) | 1 MiB | ~4.5s (**~227 KiB/s**) | ~3.7s (**~278 KiB/s**) |
| **Raw UART Limit** (No SD write) | 2 MB | ~8.7s (**~236 KiB/s**) | N/A |

*Note: These are setup-specific measurements, not universal guarantees. They should be re-measured after protocol, buffer, UART, or hardware changes. Performance depends on the adapter, wiring, UART configuration and SD card.*

## Verification status

The current live regression on the validated setup passed **10/10 tests**:

- setup and connection
- device info query
- directory creation and listing
- upload of multiple file sizes
- download and verification of multiple file sizes
- CRC32 validation
- delete operations
- streaming upload
- UART throughput benchmark

This is a strong functional verification for the current firmware and host implementation, but it is not the same as a final public release gate. Any protocol, serial, or performance-affecting change should be followed by a fresh measurement and live regression run.

---

## Repository Structure

```
esp-idf-uart-filebridge/
├── include/                       # Public API headers
├── src/                           # Component implementation
│   └── internal/                  # Private implementation headers
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

- **Binary protocol** with CRC32 integrity verification and sequence numbering.
- **Full filesystem operations:** upload, download, list, delete, rename, mkdir, copy, hash.
- **Streaming upload** (no per-chunk ACK) with Hardware Flow Control (RTS/CTS) for maximum throughput.
- **Log suppression** during transfers for optimal SD card write performance.
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
✓ Created directory /sd/models

▶ Upload ML models (upload_dir)
⠹ Uploading ━━━━━━━━━━━━ 67% 2/3 files
✓ Uploaded 3 files

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
4. **AI responds:** "✓ Successfully uploaded 3 models to /sd/models/"

**No custom Python script needed!** 🎉

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

## Hardware Requirements

- ESP32 with UART peripheral (any variant).
- USB-UART adapter (recommended: FT232R, tested @ 3 Mbit/s + HW Flow Control).
  - *Compatible with: FT232R, CH343P, CP2102N, or any USB-UART chip supporting >= 3 Mbit/s + RTS/CTS.*
- SD card formatted as FAT32 or exFAT.

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

All pins and transfer parameters are configurable via `idf.py menuconfig` under
`Component config -> ESP UART File Bridge`:

| Option | Default | Description |
|---|---|---|
| `UART_FILEBRIDGE_NUM` | 1 | UART port number |
| `UART_FILEBRIDGE_TX_PIN` | 30 | TX GPIO |
| `UART_FILEBRIDGE_RX_PIN` | 31 | RX GPIO |
| `UART_FILEBRIDGE_RTS_PIN` | 50 | RTS GPIO (HW flow ctrl) |
| `UART_FILEBRIDGE_CTS_PIN` | 29 | CTS GPIO (HW flow ctrl) |
| `UART_FILEBRIDGE_BAUD` | 3000000 | Baud rate |
| `UART_FILEBRIDGE_RX_BUF_SIZE` | 8192 | RX buffer size |
| `UART_FILEBRIDGE_TASK_STACK` | 16384 | RX task stack size |
| `UART_FILEBRIDGE_TASK_PRIO` | 5 | RX task priority |
| `UART_FILEBRIDGE_P4_LDO_ENABLE` | y (P4 only) | Enable ESP32-P4 SD LDO power ctrl |
| `UART_FILEBRIDGE_P4_LDO_CHAN` | 4 | LDO channel for P4 SD power |

## Protocol Architecture

The underlying binary protocol is designed for minimal overhead while guaranteeing data integrity.

**Binary frame format:**
```
[MAGIC_0=0xF1] [MAGIC_1=0x1E] [VERSION] [CMD] [FLAGS] [SEQ_LSB] [SEQ_MSB] [LEN_LSB] [LEN_MSB] [PAYLOAD...] [CRC32]
```

- **Frame size:** 9 byte header + payload (max 32 KB) + 4 byte CRC32.
- **Chunk size:** Defaults to 8 KB to perfectly align with optimal SD card sector writes.

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

## License

The Unlicense - see LICENSE file. The software is released into the public
domain where legally possible, with no attribution requirement and no warranty.
