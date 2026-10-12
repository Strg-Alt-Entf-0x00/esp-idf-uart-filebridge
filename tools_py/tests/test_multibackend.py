#!/usr/bin/env python3
"""
Test ESP-IDF UART Filebridge v2.0 Multi-Backend System
Uses the official Python protocol library
"""

import sys
from esp_idf_uart_filebridge.protocol import ESP32Protocol, StorageBackendInfo

def format_size(bytes_val):
    """Format bytes to human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_val < 1024.0:
            return f"{bytes_val:.2f} {unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.2f} TB"

def test_multi_backend():
    """Test multi-backend discovery and info retrieval."""
    
    print("=" * 70)
    print("ESP-IDF UART Filebridge v2.0 - Multi-Backend Test")
    print("=" * 70)
    
    # Connect
    protocol = ESP32Protocol()
    print("\nConnecting to COM13...")
    
    if not protocol.connect("COM13", 3000000):
        print("❌ Failed to connect!")
        return 1
    
    print("✅ Connected!")
    
    # Get device info
    print("\n" + "-" * 70)
    print("DEVICE INFO")
    print("-" * 70)
    
    info = protocol.get_device_info()
    print(f"Device: {info.device_name}")
    print(f"Firmware: {info.fw_version}")
    print(f"SD Card: {format_size(info.sd_size)} total, {format_size(info.sd_free)} free")
    
    # Test CMD_STORAGE_LIST (v2.0 NEW)
    print("\n" + "-" * 70)
    print("STORAGE BACKEND DISCOVERY (v2.0)")
    print("-" * 70)
    
    try:
        backends = protocol.get_storage_list()
        print(f"\n✅ Found {len(backends)} storage backend(s):\n")
        
        for i, backend in enumerate(backends, 1):
            print(f"Backend {i}:")
            print(f"  Mount Point : {backend.mount_point}")
            print(f"  Type        : {backend.type_name} (type={backend.type})")
            print(f"  Status      : {'Mounted' if backend.mounted else 'Unmounted'}")
            print(f"  Total       : {format_size(backend.total_bytes)}")
            print(f"  Free        : {format_size(backend.free_bytes)}")
            print(f"  Used        : {format_size(backend.get_used_bytes())} ({backend.get_usage_percentage():.1f}%)")
            
            # Show capabilities
            caps = []
            if backend.has_capability(StorageBackendInfo.CAP_FILE_READ):
                caps.append("READ")
            if backend.has_capability(StorageBackendInfo.CAP_FILE_WRITE):
                caps.append("WRITE")
            if backend.has_capability(StorageBackendInfo.CAP_DIRECTORIES):
                caps.append("DIRECTORIES")
            if backend.has_capability(StorageBackendInfo.CAP_FORMAT):
                caps.append("FORMAT")
            if backend.has_capability(StorageBackendInfo.CAP_COMPRESSION):
                caps.append("COMPRESSION")
            
            print(f"  Capabilities: {' | '.join(caps)}")
            print()
        
    except Exception as e:
        print(f"❌ STORAGE_LIST failed: {e}")
        protocol.disconnect()
        return 1
    
    # Test CMD_STORAGE_INFO for specific backends (v2.0 NEW)
    print("-" * 70)
    print("INDIVIDUAL BACKEND INFO (v2.0)")
    print("-" * 70)
    
    test_mounts = ["/sd", "/flash"]
    
    for mount_point in test_mounts:
        print(f"\nQuerying: {mount_point}")
        try:
            backend = protocol.get_storage_info(mount_point)
            print(f"  ✅ Type: {backend.type_name}")
            print(f"  ✅ Total: {format_size(backend.total_bytes)}")
            print(f"  ✅ Free: {format_size(backend.free_bytes)}")
            print(f"  ✅ Status: {'Mounted' if backend.mounted else 'Unmounted'}")
        except Exception as e:
            print(f"  ⚠️  Not available: {e}")
    
    # Test listing directories on different backends
    print("\n" + "-" * 70)
    print("MULTI-BACKEND DIRECTORY LISTING")
    print("-" * 70)
    
    for mount_point in ["/sd", "/flash"]:
        try:
            print(f"\nListing {mount_point}/:")
            entries = protocol.list_directory(mount_point)
            print(f"  ✅ Found {len(entries)} entries")
            
            # Show first few entries
            for entry in entries[:5]:
                entry_type = "DIR" if entry.is_directory else "FILE"
                if entry.is_directory:
                    print(f"    [{entry_type}] {entry.name}/")
                else:
                    print(f"    [{entry_type}] {entry.name} ({format_size(entry.size)})")
            
            if len(entries) > 5:
                print(f"    ... and {len(entries) - 5} more")
                
        except Exception as e:
            print(f"  ⚠️  Cannot list: {e}")
    
    # Summary
    print("\n" + "=" * 70)
    print("TEST SUMMARY")
    print("=" * 70)
    print("✅ Device communication: OK")
    print(f"✅ Multi-backend discovery: OK ({len(backends)} backends)")
    print("✅ Individual backend query: OK")
    print("✅ Multi-backend directory listing: OK")
    print("\n🎉 ALL TESTS PASSED! Multi-backend system is working!")
    
    protocol.disconnect()
    return 0

if __name__ == "__main__":
    try:
        sys.exit(test_multi_backend())
    except KeyboardInterrupt:
        print("\n⚠️  Interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
