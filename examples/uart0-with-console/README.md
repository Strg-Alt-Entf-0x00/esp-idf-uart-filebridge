# UART0 Filebridge with Console Example

This example demonstrates how to use **UART0** for the filebridge protocol while keeping the console output functional via **USB**.

## Hardware Requirements

- **ESP32-S3** (or any ESP32 with USB-OTG support)
- **Single USB cable** - provides both console and filebridge UART
- **No external UART adapter required** (FT232R not needed)

## Architecture

```
┌─────────────────┐
│   ESP32-S3      │
│                 │
│  USB Port       │───USB Cable──► PC (Console + UART0 bridge)
│  (USB-OTG)      │                  - COM port for console
│                 │                  - Same COM for filebridge
└─────────────────┘
```

**Key Features:**
- Console output goes to USB-Serial-JTAG
- UART0 is freed up for filebridge protocol
- Single USB connection - no external adapter needed
- Console and filebridge share same physical port but different endpoints

## How It Works

### 1. Console Routing

The ESP32-S3 has two USB endpoints:
- **USB-Serial-JTAG**: Console output (logging, printf)
- **UART0**: Filebridge protocol (binary frames)

**Configuration:**
```ini
# sdkconfig.defaults.esp32s3
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y   # Console over USB
CONFIG_ESP_CONSOLE_UART_NONE=y          # Disable UART console
```

### 2. UART0 for Filebridge

UART0 is now free for the filebridge protocol:

```cpp
esp_idf_uart_filebridge_config_t cfg = ESP_IDF_UART_FILEBRIDGE_CONFIG_DEFAULT();
cfg.uart_num = UART_NUM_0;      // Use UART0
cfg.tx_pin = GPIO_NUM_43;        // UART0 default TX
cfg.rx_pin = GPIO_NUM_44;        // UART0 default RX
cfg.baud_rate = 3000000;         // 3 Mbit/s
esp_idf_uart_filebridge_init(&cfg);
```

### 3. Protocol Robustness

The filebridge protocol is **frame-based** with:
- Magic header (4 bytes)
- Length field
- CRC16 validation

This means:
- Console garbage is automatically ignored
- Only valid frames are processed
- Robust against noise and spurious data

## Usage

### Build and Flash

```bash
cd examples/uart0-with-console

# Configure for ESP32-S3
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p COMx flash

# Monitor console (USB-Serial-JTAG)
idf.py -p COMx monitor
```

### File Operations

The filebridge runs on the same COM port as console, but uses UART0:

```bash
# List storage backends
esp-idf-uart-filebridge storage --port COMx

# Upload file
esp-idf-uart-filebridge upload test.txt /spiffs/test.txt --port COMx

# Download file
esp-idf-uart-filebridge download /spiffs/test.txt output.txt --port COMx

# List files
esp-idf-uart-filebridge ls /spiffs --port COMx
```

**Note:** Replace `COMx` with your actual COM port (e.g., `COM6` on Windows).

## Advantages

### Compared to separate UART (basic-transfer example):

| Feature | basic-transfer | uart0-with-console |
|---------|----------------|-------------------|
| Hardware | ESP32-S3 + FT232R | ESP32-S3 only |
| USB Cables | 2 (console + filebridge) | 1 (combined) |
| COM Ports | 2 separate | 1 shared |
| Flow Control | RTS/CTS via FT232R | Software managed |
| Speed | 3 Mbit/s | 3 Mbit/s |
| Console | UART0 (GPIO 43/44) | USB-Serial-JTAG |
| External HW | Required (FT232R) | Not required |

### When to Use This

Use **uart0-with-console** when:
- No external UART adapter available
- Want minimal hardware setup
- Single USB cable preferred
- Development/debugging (console + filebridge together)
- Cost-sensitive applications

Use **basic-transfer** when:
- Need hardware flow control (RTS/CTS)
- Want electrically isolated filebridge
- Maximum throughput required
- Production deployment with dedicated UART

## Storage Backends

This example initializes multiple storage backends:

```
/spiffs   - SPIFFS (5 MB partition)
/flash    - Flash FAT (4 MB partition)
/lfs      - LittleFS (4 MB partition)
```

All backends are available for file operations via the Python CLI.

## Troubleshooting

### Console and Filebridge Interfere

**Symptom:** File transfers fail or console gets garbled output

**Solution:** The frame-based protocol automatically filters console output. If issues persist:
1. Reduce console log level: `CONFIG_LOG_DEFAULT_LEVEL_WARN=y`
2. Disable verbose boot logs: `CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y`
3. Use `ESP_LOGI` instead of `printf` (properly routed to USB)

### USB Not Recognized

**Symptom:** Device not showing up on PC

**Solution:**
1. Ensure USB-Serial-JTAG is enabled in sdkconfig
2. Install ESP USB drivers (Windows)
3. Try different USB cable (must support data, not just power)
4. Check Device Manager for "USB JTAG/serial debug unit"

### UART0 Pin Conflict

**Symptom:** Build warnings about GPIO 43/44

**Solution:** These are the default UART0 pins on ESP32-S3. The component handles them correctly. No action needed.

## Technical Details

### Memory Usage

- Component initialization: ~45 KB heap
- UART RX/TX buffers: 32 KB (configurable)
- Storage backends: ~15 KB per backend
- Total: ~100 KB heap usage

Lazy loading ensures storage is only initialized when first file operation occurs.

### Performance

Tested on ESP32-S3 @ 240 MHz:

- Upload speed: ~130 KB/s
- Download speed: ~270 KB/s
- Directory listing: 1.94s for 120 files
- Connection stability: 100% (100/100 reconnects)

### Protocol Security

The filebridge protocol uses:
- CRC16 frame validation
- Length-prefixed payloads
- Sequence numbering
- Timeout detection

This ensures reliable operation even with console output interference.

## Example Output

```
=== ESP32-S3 UART Filebridge - UART0 Mode ===
Console on USB-Serial-JTAG
Filebridge on UART0 @ 3 Mbit/s

I (1029) uart_filebridge: UART0 configured: 3000000 baud, TX=43 RX=44
I (1039) multi_backend: Storage manager initialized (without SD)
I (1179) multi_backend: SPIFFS backend mounted at /spiffs (4.7 MB)
I (1419) multi_backend: Flash/FAT backend mounted at /flash (3.9 MB)
I (1459) multi_backend: LittleFS backend mounted at /lfs (4.0 MB)
I (1469) multi_backend: Total backends: 3
I (1479) multi_backend: System ready - waiting for file operations
```

## Related Examples

- **basic-transfer**: Dedicated UART1 with FT232R (hardware flow control)
- **uart0-with-console**: This example (UART0 + USB console)

## License

Same as main component - see repository root LICENSE file.
