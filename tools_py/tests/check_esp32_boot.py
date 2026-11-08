#!/usr/bin/env python3
"""Check if ESP32 booted successfully by reading COM6 console."""
import serial
import time

PORT = "COM6"
BAUD = 115200

print("=" * 70)
print("ESP32 Boot Status Check")
print("=" * 70)
print(f"Port: {PORT} @ {BAUD} baud")
print("Waiting for port to be available...")
print()

# Try to open port
for attempt in range(10):
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.5)
        print(f"[PASS] Port {PORT} opened successfully")
        break
    except serial.SerialException as e:
        print(f"[{attempt+1}/10] Port busy, waiting... ({e})")
        time.sleep(1)
else:
    print(f"\n[FAIL] Could not open {PORT} after 10 attempts")
    print("Close any programs using COM6 and try again")
    exit(1)

print("=" * 70)
print("Reading boot messages (press Ctrl+C to stop)...")
print("=" * 70)
print()

# Clear buffer
ser.reset_input_buffer()

# Send a reset pulse (DTR toggle)
try:
    ser.setDTR(False)
    time.sleep(0.1)
    ser.setDTR(True)
    time.sleep(0.5)
    print("[INFO] Sent reset pulse via DTR")
except:
    print("[WARN] Could not send reset pulse")

# Read for 10 seconds
start_time = time.time()
watchdog_count = 0
successful_boot = False

try:
    while time.time() - start_time < 10:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            text = data.decode('utf-8', errors='replace')
            print(text, end='', flush=True)
            
            # Check for watchdog reset
            if 'TG1WDT_SYS_RST' in text:
                watchdog_count += 1
            
            # Check for successful boot messages
            if 'Multi-backend system ready' in text or 'UART file bridge initialized' in text:
                successful_boot = True
        else:
            time.sleep(0.1)
except KeyboardInterrupt:
    print("\n[INFO] Stopped by user")

ser.close()

print()
print("=" * 70)
print("BOOT ANALYSIS")
print("=" * 70)
print(f"Watchdog resets detected: {watchdog_count}")
print(f"Successful boot: {'YES' if successful_boot else 'NO'}")
print()

if watchdog_count > 0:
    print("[FAIL] ESP32 is stuck in watchdog reset loop!")
    print("This means GPIO pins or SD card mounting is causing crashes.")
elif successful_boot:
    print("[PASS] ESP32 booted successfully!")
    print("UART filebridge should be ready on COM13")
else:
    print("[UNKNOWN] Could not determine boot status")
    print("No output received - check USB cable or press RESET button")

print("=" * 70)
