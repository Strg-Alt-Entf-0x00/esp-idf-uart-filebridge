"""
Comprehensive Test and Benchmark Suite for ESP32 Classic
Tests all core Filebridge functionalities on ESP32 (single-cable UART0 @ 921600 baud)
"""

import os
import sys
import time
import binascii
import hashlib

from esp_idf_uart_filebridge.protocol import ESP32Protocol
from esp_idf_uart_filebridge.file_manager import ESP32FileManager

PORT = "COM8"
BAUD = 921600

def run_suite():
    print("=" * 75)
    print(" ESP-IDF UART FILEBRIDGE - ESP32 CLASSIC COMPREHENSIVE VALIDATION SUITE")
    print("=" * 75)
    print(f"Connecting to {PORT} @ {BAUD} baud...")
    
    proto = ESP32Protocol()
    if not proto.connect(PORT, BAUD):
        print(f"ERROR: Could not connect to {PORT}")
        sys.exit(1)
        
    fm = ESP32FileManager(proto)
    
    # 1. Device Info
    print("\n[Step 1] Device Information & Handshake")
    info = proto.get_device_info()
    print(f"  Device Name        : {info.device_name}")
    print(f"  Firmware Version   : {info.fw_version}")
    print(f"  Chunk Size         : {proto.chunk_size} bytes")
    print(f"  Max Payload        : {proto.max_payload} bytes")
    assert info.fw_version == "2.0.0", f"Unexpected FW version: {info.fw_version}"
    print("  -> Device Handshake: PASS")

    # 2. Directory Listing
    print("\n[Step 2] Directory Listing on /spiffs/")
    entries = fm.list_directory("/spiffs")
    print(f"  Files currently on /spiffs: {len(entries)}")
    for e in entries:
        print(f"    - {e.name:<25} ({e.size:,} bytes)")
    print("  -> Directory Listing: PASS")

    # 3. Small, Medium, Large Benchmark Tests
    test_specs = [
        {"name": "tiny_text.txt", "size": 256, "label": "256 B (Tiny Text)"},
        {"name": "file_5kb.bin",  "size": 5 * 1024, "label": "5 KB (Medium Binary)"},
        {"name": "file_10kb.bin", "size": 10 * 1024, "label": "10 KB (Medium-Large)"},
        {"name": "file_20kb.bin", "size": 20 * 1024, "label": "20 KB (Large Binary)"},
    ]

    benchmark_results = []

    print("\n[Step 3] Running Upload, Download & Integrity Tests")
    for spec in test_specs:
        file_name = spec["name"]
        file_size = spec["size"]
        label = spec["label"]
        remote_path = f"/spiffs/{file_name}"
        local_src = f"tmp_src_{file_name}"
        local_dst = f"tmp_dst_{file_name}"

        print(f"\n  Testing {label}...")

        # Generate test payload
        if file_name.endswith(".txt"):
            test_data = (b"Hello ESP-IDF UART Filebridge! Rapid data transfer test.\n" * 5)[:file_size]
        else:
            test_data = os.urandom(file_size)

        with open(local_src, "wb") as f:
            f.write(test_data)

        local_crc = binascii.crc32(test_data) & 0xFFFFFFFF
        local_sha = hashlib.sha256(test_data).hexdigest()

        # A) Upload Benchmark
        t0 = time.time()
        fm.upload_file(local_src, remote_path)
        t_upload = time.time() - t0
        up_speed_kb = (file_size / 1024.0) / t_upload
        print(f"    Upload     : {file_size:,} bytes in {t_upload:.3f}s ({up_speed_kb:.1f} KB/s)")

        # B) On-chip CRC32 Calculation
        t0 = time.time()
        remote_crc = fm.get_file_hash(remote_path)
        t_hash = time.time() - t0
        if local_crc != remote_crc:
            print(f"    ERROR: CRC mismatch! Local={local_crc:08X}, Remote={remote_crc:08X}")
            sys.exit(1)
        print(f"    On-chip CRC: {remote_crc:08X} in {t_hash:.3f}s (MATCH)")

        # C) Download Benchmark
        t0 = time.time()
        fm.download_file(remote_path, local_dst)
        t_download = time.time() - t0
        down_speed_kb = (file_size / 1024.0) / t_download
        print(f"    Download   : {file_size:,} bytes in {t_download:.3f}s ({down_speed_kb:.1f} KB/s)")

        # D) Verification of downloaded file
        with open(local_dst, "rb") as f:
            downloaded_data = f.read()

        dl_sha = hashlib.sha256(downloaded_data).hexdigest()
        assert local_sha == dl_sha, f"SHA256 mismatch on {file_name}!"
        print(f"    SHA-256    : {dl_sha[:16]}... (100% BIT-PERFECT MATCH)")

        # E) Stat Check
        st = fm.get_file_stat(remote_path)
        assert st.size == file_size, f"Stat size mismatch: {st.size} != {file_size}"
        print(f"    Stat Check : Size {st.size} bytes (MATCH)")

        # F) Deletion
        fm.delete_file(remote_path)
        print(f"    Clean Delete: {remote_path} removed")

        # Cleanup local scratch files
        if os.path.exists(local_src):
            os.remove(local_src)
        if os.path.exists(local_dst):
            os.remove(local_dst)

        benchmark_results.append({
            "label": label,
            "size_bytes": file_size,
            "up_speed": up_speed_kb,
            "t_up": t_upload,
            "down_speed": down_speed_kb,
            "t_down": t_download,
            "status": "PASS"
        })
        time.sleep(0.5)

    proto.disconnect()

    # Final summary table
    print("\n" + "=" * 75)
    print("                     FINAL BENCHMARK RESULTS TABLE")
    print("=" * 75)
    print(f"{'Test Case':<22} | {'Upload Speed':<20} | {'Download Speed':<20} | {'Integrity':<8}")
    print("-" * 75)
    for r in benchmark_results:
        up_str = f"{r['up_speed']:6.1f} KB/s ({r['t_up']:.2f}s)"
        dl_str = f"{r['down_speed']:6.1f} KB/s ({r['t_down']:.2f}s)"
        print(f"{r['label']:<22} | {up_str:<20} | {dl_str:<20} | {r['status']:<8}")
    print("=" * 75)
    print("ALL TESTS COMPLETED SUCCESSFULLY! ESP32 Classic is fully verified.")
    print("=" * 75)

if __name__ == "__main__":
    run_suite()
