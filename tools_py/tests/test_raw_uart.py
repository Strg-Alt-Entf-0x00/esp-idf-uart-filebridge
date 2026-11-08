#!/usr/bin/env python3
"""Raw UART test to diagnose hardware connection issues."""

import serial
import time
import sys

PORT = "COM13"  # CHANGEME: Replace with your actual port
BAUD = 3000000

print("=" * 70)
print("Raw UART Connection Test")
print("=" * 70)
print(f"Port: {PORT}")
print(f"Baud: {BAUD}")
print()

# Test 1: Open port WITHOUT flow control
print("TEST 1: Open port without RTS/CTS")
print("-" * 70)
try:
    ser = serial.Serial(
        PORT,
        BAUD,
        timeout=1.0,
        rtscts=False,  # NO flow control
        dsrdtr=False,
        xonxoff=False
    )
    print(f"[PASS] Port {PORT} opened successfully")
    print(f"  Baudrate: {ser.baudrate}")
    print(f"  RTS/CTS:  {ser.rtscts}")
    print(f"  Timeout:  {ser.timeout}s")
    print()
    
    # Clear any existing data
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    
    # Test 2: Send raw bytes
    print("TEST 2: Send HELLO packet (without flow control)")
    print("-" * 70)
    # HELLO packet: Magic[2] + Cmd[1] + Seq[2] + Len[4] + CRC32[4]
    # F1 1E 01 00 00 00 00 00 00 + CRC
    hello_packet = bytes([0xF1, 0x1E, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
    
    import binascii
    crc = binascii.crc32(hello_packet) & 0xFFFFFFFF
    hello_packet += crc.to_bytes(4, 'little')
    
    print(f"Sending: {hello_packet.hex(' ')}")
    ser.write(hello_packet)
    ser.flush()
    print("[SENT] HELLO packet sent")
    print()
    
    # Test 3: Wait for response
    print("TEST 3: Wait for response (3 seconds)")
    print("-" * 70)
    start = time.time()
    received = bytearray()
    
    while (time.time() - start) < 3.0:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            received.extend(data)
            print(f"[RX] Received {len(data)} bytes: {data.hex(' ')}")
    
    if len(received) == 0:
        print("[FAIL] No response received!")
        print()
        print("This means:")
        print("  1. TX/RX wiring is wrong (not crossed), OR")
        print("  2. ESP32 is not running, OR")
        print("  3. ESP32 UART0 is not initialized")
        print()
        print("Check COM6 console to see if ESP32 is running!")
    else:
        print(f"[PASS] Received {len(received)} bytes total")
        print(f"Full data: {received.hex(' ')}")
        
        # Check if it looks like a valid response
        if len(received) >= 2 and received[0] == 0xF1 and received[1] == 0x1E:
            print("[PASS] Response has valid protocol magic!")
            if len(received) >= 3:
                cmd = received[2]
                cmd_names = {0x10: "ACK", 0x11: "NACK"}
                print(f"[INFO] Command: 0x{cmd:02X} ({cmd_names.get(cmd, 'UNKNOWN')})")
        else:
            print("[WARN] Response doesn't match protocol format")
    
    ser.close()
    print()
    
except PermissionError:
    print(f"[FAIL] Port {PORT} is in use by another program!")
    print("Close any serial monitors, terminals, or other tools using this port.")
    sys.exit(1)
except Exception as e:
    print(f"[FAIL] Error: {e}")
    sys.exit(1)

# Test 4: Try WITH flow control
print("=" * 70)
print("TEST 4: Repeat with RTS/CTS flow control")
print("-" * 70)
try:
    ser = serial.Serial(
        PORT,
        BAUD,
        timeout=1.0,
        rtscts=True,  # WITH flow control
        dsrdtr=False,
        xonxoff=False
    )
    print(f"[PASS] Port {PORT} opened with RTS/CTS")
    
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    
    # Send HELLO again
    ser.write(hello_packet)
    ser.flush()
    print("[SENT] HELLO packet sent (with flow control)")
    
    # Wait for response
    start = time.time()
    received = bytearray()
    
    while (time.time() - start) < 3.0:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            received.extend(data)
            print(f"[RX] Received {len(data)} bytes: {data.hex(' ')}")
    
    if len(received) == 0:
        print("[FAIL] No response with RTS/CTS enabled!")
        print()
        print("This means RTS/CTS wiring is incorrect:")
        print("  ESP32 RTS (GPIO 1) MUST connect to FT232R CTS")
        print("  ESP32 CTS (GPIO 2) MUST connect to FT232R RTS")
        print()
        print("Solution: Either fix the wiring, OR disable flow control in firmware.")
    else:
        print(f"[PASS] Received {len(received)} bytes with RTS/CTS!")
        print("[SUCCESS] Hardware flow control is working correctly!")
    
    ser.close()
    
except Exception as e:
    print(f"[FAIL] Error with flow control: {e}")

print()
print("=" * 70)
print("DIAGNOSIS COMPLETE")
print("=" * 70)
