"""
Interactive shell for ESP32 UART Filebridge.

Keeps serial connection open to avoid DTR reset on external USB-UART chips (CH340, CP2102, FTDI).
Similar to mpremote, rshell, and other professional embedded tools.
"""

import sys
import os
from pathlib import Path
from .protocol import ESP32Protocol, ESP32ProtocolError
from .file_manager import ESP32FileManager


class InteractiveShell:
    """Interactive shell with persistent connection."""
    
    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.protocol = None
        self.file_manager = None
        self.connected = False
        
    def connect(self):
        """Establish persistent connection."""
        print(f"🔌 Connecting to {self.port} @ {self.baud} baud...")
        
        self.protocol = ESP32Protocol()
        success = self.protocol.connect(self.port, self.baud)
        
        if not success:
            print(f"❌ Failed to connect to {self.port}")
            return False
        
        self.file_manager = ESP32FileManager(self.protocol)
        self.connected = True
        
        print(f"✅ Connected! Connection stays open - no DTR resets.")
        print(f"Type 'help' for commands, 'exit' to quit.\n")
        return True
    
    def disconnect(self):
        """Close connection."""
        if self.protocol:
            self.protocol.disconnect()
            self.connected = False
            print("👋 Disconnected.")
    
    def cmd_help(self, args):
        """Show available commands."""
        print("""
Available commands:

  Storage:
    storage-list                    List all storage backends
    ls <path>                       List directory contents
    
  File Transfer:
    upload <local> <remote>         Upload file to ESP32
    download <remote> <local>       Download file from ESP32
    cat <path>                      Show file contents
    rm <path>                       Delete file
    
  System:
    help                            Show this help
    exit, quit                      Exit shell
    
Examples:
    ls /spiffs/
    upload test.txt /spiffs/test.txt
    download /spiffs/data.bin ./data.bin
    cat /spiffs/config.json
    rm /spiffs/old_file.txt
""")
    
    def cmd_storage_list(self, args):
        """List storage backends."""
        try:
            backends = self.protocol.storage_list()
            
            print("\nRegistered Storage Backends:")
            print(f"  {'Mount':<12} {'Type':<12} {'Status':<10} {'Total':<12} {'Free':<12} {'Used %':<8}")
            print(f"  {'-'*10:<12} {'-'*10:<12} {'-'*10:<10} {'-'*10:<12} {'-'*10:<12} {'-'*6:<8}")
            
            for backend in backends:
                mount = backend.get('mount_point', '?')
                type_name = backend.get('type_name', '?')
                status = "Mounted" if backend.get('is_mounted', False) else "Unmounted"
                total = backend.get('total_bytes', 0)
                free = backend.get('free_bytes', 0)
                
                if total > 0:
                    total_str = f"{total // 1024} KB"
                    free_str = f"{free // 1024} KB"
                    used_pct = f"{100 - (free * 100 // total)}%"
                else:
                    total_str = "N/A"
                    free_str = "N/A"
                    used_pct = "N/A"
                
                print(f"  {mount:<12} {type_name:<12} {status:<10} {total_str:<12} {free_str:<12} {used_pct:<8}")
            
            print()
            
        except ESP32ProtocolError as e:
            print(f"❌ Error: {e}")
    
    def cmd_ls(self, args):
        """List directory."""
        if not args:
            print("❌ Usage: ls <path>")
            return
        
        path = args[0]
        try:
            print(f"\n📁 Listing {path}:")
            entries = self.protocol.list_directory(path)
            
            if not entries:
                print("  (empty)")
            else:
                for entry in entries:
                    name = entry.get('name', '?')
                    is_dir = entry.get('is_directory', False)
                    size = entry.get('size', 0)
                    
                    if is_dir:
                        print(f"  📁 {name}/")
                    else:
                        print(f"  📄 {name:<30} {size:>10} bytes")
            print()
            
        except ESP32ProtocolError as e:
            print(f"❌ Error: {e}")
    
    def cmd_upload(self, args):
        """Upload file."""
        if len(args) < 2:
            print("❌ Usage: upload <local_file> <remote_path>")
            return
        
        local_file = Path(args[0])
        remote_path = args[1]
        
        if not local_file.exists():
            print(f"❌ Local file not found: {local_file}")
            return
        
        try:
            print(f"⬆️  Uploading {local_file} → {remote_path}")
            success = self.file_manager.upload_file(
                str(local_file),
                remote_path,
                progress_callback=lambda p, t: print(f"\r  Progress: {p}/{t} bytes ({100*p//t}%)", end="")
            )
            print()  # Newline after progress
            
            if success:
                print(f"✅ Upload complete!")
            else:
                print(f"❌ Upload failed")
                
        except ESP32ProtocolError as e:
            print(f"\n❌ Error: {e}")
    
    def cmd_download(self, args):
        """Download file."""
        if len(args) < 2:
            print("❌ Usage: download <remote_path> <local_file>")
            return
        
        remote_path = args[0]
        local_file = Path(args[1])
        
        try:
            print(f"⬇️  Downloading {remote_path} → {local_file}")
            success = self.file_manager.download_file(
                remote_path,
                str(local_file),
                progress_callback=lambda p, t: print(f"\r  Progress: {p}/{t} bytes ({100*p//t}%)", end="")
            )
            print()  # Newline after progress
            
            if success:
                print(f"✅ Download complete!")
            else:
                print(f"❌ Download failed")
                
        except ESP32ProtocolError as e:
            print(f"\n❌ Error: {e}")
    
    def cmd_cat(self, args):
        """Show file contents."""
        if not args:
            print("❌ Usage: cat <path>")
            return
        
        path = args[0]
        try:
            content = self.protocol.read_file(path)
            print(f"\n📄 Contents of {path}:")
            print("-" * 60)
            print(content.decode('utf-8', errors='replace'))
            print("-" * 60)
            print()
            
        except ESP32ProtocolError as e:
            print(f"❌ Error: {e}")
    
    def cmd_rm(self, args):
        """Delete file."""
        if not args:
            print("❌ Usage: rm <path>")
            return
        
        path = args[0]
        confirm = input(f"⚠️  Delete {path}? (y/N): ")
        
        if confirm.lower() != 'y':
            print("Cancelled.")
            return
        
        try:
            success = self.protocol.delete_file(path)
            if success:
                print(f"✅ Deleted {path}")
            else:
                print(f"❌ Delete failed")
                
        except ESP32ProtocolError as e:
            print(f"❌ Error: {e}")
    
    def run(self):
        """Run interactive shell."""
        if not self.connect():
            return 1
        
        # Command dispatch table
        commands = {
            'help': self.cmd_help,
            '?': self.cmd_help,
            'storage-list': self.cmd_storage_list,
            'storage': self.cmd_storage_list,
            'ls': self.cmd_ls,
            'dir': self.cmd_ls,
            'upload': self.cmd_upload,
            'put': self.cmd_upload,
            'download': self.cmd_download,
            'get': self.cmd_download,
            'cat': self.cmd_cat,
            'type': self.cmd_cat,
            'rm': self.cmd_rm,
            'del': self.cmd_rm,
        }
        
        try:
            while self.connected:
                try:
                    # Read command
                    line = input("esp32> ").strip()
                    
                    if not line:
                        continue
                    
                    # Parse command
                    parts = line.split()
                    cmd = parts[0].lower()
                    args = parts[1:]
                    
                    # Exit commands
                    if cmd in ['exit', 'quit', 'q']:
                        break
                    
                    # Execute command
                    if cmd in commands:
                        commands[cmd](args)
                    else:
                        print(f"❌ Unknown command: {cmd}")
                        print("   Type 'help' for available commands")
                
                except KeyboardInterrupt:
                    print("\n^C (Ctrl+C to exit, or type 'exit')")
                    continue
                
                except EOFError:
                    print()
                    break
        
        finally:
            self.disconnect()
        
        return 0


def main(port: str, baud: int = 115200):
    """Entry point for interactive shell."""
    shell = InteractiveShell(port, baud)
    return shell.run()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python -m esp_idf_uart_filebridge.interactive <port> [baud]")
        sys.exit(1)
    
    port = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    
    sys.exit(main(port, baud))
