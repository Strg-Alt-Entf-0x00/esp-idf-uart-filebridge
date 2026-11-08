#!/usr/bin/env python3
"""Simple connection test for ESP32 UART Filebridge.

Tests different configurations to find the right pin setup.
"""

import sys
import time
import logging
from esp_idf_uart_filebridge.protocol import ESP32Protocol

logging.basicConfig(level=logging.DEBUG, format='%(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

def test_connection(port: str, baud: int = 3000000, rtscts: bool = True) -> bool:
    """Test basic connection to ESP32."""
    proto = ESP32Protocol()
    
    logger.info(f"Testing connection to {port} @ {baud} baud...")
    logger.info(f"Hardware flow control (RTS/CTS): {rtscts}")
    
    try:
        if proto.connect(port, baud, rtscts=rtscts):
            logger.info("[PASS] CONNECTION SUCCESS!")
            
            # Try to get device info
            try:
                info = proto.get_device_info()
                logger.info(f"Device: {info.get('device_name', 'Unknown')}")
                logger.info(f"Version: {info.get('version', 'Unknown')}")
                logger.info(f"Backends: {info.get('backend_count', 0)}")
                return True
            except Exception as e:
                logger.warning(f"Connected but failed to get device info: {e}")
                return True
        else:
            logger.error("[FAIL] CONNECTION FAILED (timeout or no response)")
            return False
            
    except Exception as e:
        logger.error(f"[FAIL] CONNECTION ERROR: {e}")
        return False
    finally:
        try:
            proto.disconnect()
        except:
            pass

def test_storage_list(port: str, baud: int = 3000000, rtscts: bool = True) -> bool:
    """Test storage backend listing."""
    proto = ESP32Protocol()
    
    logger.info(f"Testing storage list on {port}...")
    
    try:
        if not proto.connect(port, baud, rtscts=rtscts):
            logger.error("[FAIL] Failed to connect")
            return False
        
        logger.info("[PASS] Connected, getting storage backends...")
        
        try:
            backends = proto.get_storage_backends()
            logger.info(f"Found {len(backends)} storage backend(s):")
            for i, backend in enumerate(backends):
                mount = backend.get('mount_point', '?')
                backend_type = backend.get('type', '?')
                mounted = backend.get('is_mounted', False)
                status = "mounted" if mounted else "not mounted"
                logger.info(f"  [{i}] {backend_type} at {mount} ({status})")
            return True
            
        except Exception as e:
            logger.error(f"Failed to get storage backends: {e}")
            return False
            
    finally:
        try:
            proto.disconnect()
        except:
            pass

def test_simple_list(port: str, path: str = "/sd", baud: int = 3000000, rtscts: bool = True) -> bool:
    """Test simple directory listing."""
    proto = ESP32Protocol()
    
    logger.info(f"Testing directory listing: {path}")
    
    try:
        if not proto.connect(port, baud, rtscts=rtscts):
            logger.error("[FAIL] Failed to connect")
            return False
        
        logger.info("[PASS] Connected, listing directory...")
        
        try:
            entries = proto.list_directory(path)
            logger.info(f"Directory {path} contains {len(entries)} items:")
            for entry in entries[:10]:  # Show first 10
                entry_type = "[DIR]" if entry.is_directory else "[FILE]"
                logger.info(f"  {entry_type} {entry.name} ({entry.size} bytes)")
            if len(entries) > 10:
                logger.info(f"  ... and {len(entries) - 10} more items")
            return True
            
        except Exception as e:
            logger.error(f"Failed to list directory: {e}")
            return False
            
    finally:
        try:
            proto.disconnect()
        except:
            pass

if __name__ == "__main__":
    print("=" * 70)
    print("ESP32 UART Filebridge Connection Test")
    print("=" * 70)
    print()
    
    # Configuration
    PORT = "COM13"  # CHANGEME: Replace with your actual port
    BAUD = 3000000  # 3 Mbit/s
    
    print(f"Configuration:")
    print(f"  Port: {PORT}")
    print(f"  Baud: {BAUD}")
    print(f"  Expected ESP32 pins: TX=GPIO4, RX=GPIO5, RTS=GPIO6, CTS=GPIO7")
    print()
    print("IMPORTANT: Verify your wiring!")
    print("  ESP32 TX (GPIO 4)  -> FT232R RX")
    print("  ESP32 RX (GPIO 5)  <- FT232R TX")
    print("  ESP32 RTS (GPIO 6) -> FT232R CTS  [MUST CROSS!]")
    print("  ESP32 CTS (GPIO 7) <- FT232R RTS  [MUST CROSS!]")
    print("  ESP32 GND          -- FT232R GND")
    print()
    print("=" * 70)
    print()
    
    # Test 1: Basic connection with hardware flow control
    print("TEST 1: Basic Connection Test (with RTS/CTS flow control)")
    print("-" * 70)
    success1 = test_connection(PORT, BAUD, rtscts=True)
    print()
    
    if not success1:
        print("[FAIL] Basic connection failed with hardware flow control!")
        print()
        print("Trying without hardware flow control...")
        print()
        
        # Test without flow control
        print("TEST 1b: Connection Test WITHOUT RTS/CTS")
        print("-" * 70)
        success1b = test_connection(PORT, BAUD, rtscts=False)
        print()
        
        if success1b:
            print("[PASS] Connection works WITHOUT flow control!")
            print("[WARN] This means RTS/CTS wiring is incorrect or not needed.")
            print()
            print("Options:")
            print("1. Fix RTS/CTS wiring (recommended for 3 Mbit/s):")
            print("   ESP32 RTS (GPIO 1) -> FT232R CTS")
            print("   ESP32 CTS (GPIO 2) <- FT232R RTS")
            print()
            print("2. Disable flow control in firmware (works but risks data loss):")
            print("   Edit sdkconfig.defaults.esp32s3_ft232r:")
            print("     CONFIG_UART_FILEBRIDGE_RTS_PIN=-1")
            print("     CONFIG_UART_FILEBRIDGE_CTS_PIN=-1")
            print("   Rebuild and flash")
            print()
            success1 = True  # Continue tests
        else:
            print("[FAIL] Connection failed even without flow control!")
            print()
            print("Troubleshooting:")
            print("1. Check TX/RX are crossed (ESP32 TX -> FT232R RX)")
            print("2. Verify ESP32 is running (check console port)")
            print("3. Verify COM port is correct (check Device Manager / dmesg)")
            print("4. Check GND connection")
            sys.exit(1)
    
    # Test 2: Get storage backends
    print("TEST 2: Storage Backend Discovery")
    print("-" * 70)
    success2 = test_storage_list(PORT, BAUD)
    print()
    
    if not success2:
        print("[WARN] Storage backend test failed (but basic connection worked)")
        print("This might indicate SD card not mounted or backend initialization issue.")
        print()
    
    # Test 3: List SD card directory
    print("TEST 3: SD Card Directory Listing")
    print("-" * 70)
    success3 = test_simple_list(PORT, "/sd", BAUD)
    print()
    
    if not success3:
        print("[WARN] SD card listing failed")
        print("Possible causes:")
        print("  - SD card not inserted")
        print("  - SD card not formatted (needs FAT32)")
        print("  - SD card mount failed (check COM6 console logs)")
        print()
    
    # Summary
    print("=" * 70)
    print("TEST SUMMARY")
    print("=" * 70)
    print(f"Basic Connection:     {'[PASS]' if success1 else '[FAIL]'}")
    print(f"Storage Backends:     {'[PASS]' if success2 else '[FAIL]'}")
    print(f"SD Card Listing:      {'[PASS]' if success3 else '[FAIL]'}")
    print()
    
    if success1 and success2 and success3:
        print("[SUCCESS] ALL TESTS PASSED! Connection is working correctly.")
        print()
        print("Next steps:")
        print("  1. Test file upload: python -m esp_idf_uart_filebridge.cli upload test.txt /sd/test.txt --port <YOUR_PORT>")
        print("  2. Monitor console during transfers to watch for bugs")
        print("  3. Run comprehensive tests with large files")
    elif success1:
        print("[PASS] Connection works but storage needs attention.")
        print("Check COM6 console for SD card mount errors.")
    else:
        print("[FAIL] Connection failed. Fix wiring and try again.")
    
    sys.exit(0 if success1 else 1)
