#!/usr/bin/env python3
"""
Simple console reader for ESP32-S3 COM6
Reads boot messages to verify no watchdog resets
"""
import serial
import time
import sys

PORT = "COM6"
BAUD = 115200

def read_console():
    print(f"[INFO] Opening {PORT} @ {BAUD} baud...")
    print(f"[INFO] Press Ctrl+C to exit")
    print("=" * 70)
    
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        time.sleep(0.5)
        
        # Clear any buffered data
        ser.reset_input_buffer()
        
        print("[INFO] Reading console output...")
        print("-" * 70)
        
        while True:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                try:
                    text = data.decode('utf-8', errors='replace')
                    print(text, end='', flush=True)
                except:
                    print(data)
            else:
                time.sleep(0.1)
                
    except serial.SerialException as e:
        print(f"\n[ERROR] Serial port error: {e}")
        print(f"[HINT] Port {PORT} might be in use by another program")
        return 1
    except KeyboardInterrupt:
        print("\n\n[INFO] Stopped by user")
        return 0
    finally:
        try:
            ser.close()
        except:
            pass

if __name__ == "__main__":
    sys.exit(read_console())
