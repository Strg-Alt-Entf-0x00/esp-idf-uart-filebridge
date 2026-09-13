import sys
import os
from pathlib import Path
from esp_uart_filebridge.file_manager import ESP32FileManager
from esp_uart_filebridge.protocol import ESP32ProtocolError, ESP32Protocol

proto = ESP32Protocol()
proto.connect('COM13', 3000000)
fm = ESP32FileManager(proto)
local_dir = r'D:\github-repositorys\inflect-v2.cpp\build\_deps\espeak-static\build\espeak-ng-data'
remote_dir = '/sd/espeak-ng-data'

root = Path(local_dir)
for local_path in root.rglob('*'):
    if local_path.is_file():
        relative_path = local_path.relative_to(root).as_posix()
        remote_path = f"{remote_dir}/{relative_path}"
        remote_parent = remote_path.rsplit('/', 1)[0]
        
        try:
            proto.mkdir(remote_parent)
        except Exception:
            pass  # Ignore mkdir errors
            
        print(f'Uploading {remote_path}...')
        try:
            # Delete existing file so rename doesn't fail
            try:
                proto.delete(remote_path)
            except Exception:
                pass
            fm.upload_file(local_path, remote_path)
        except Exception as e:
            print(f"Failed to upload {remote_path}: {e}")
print('Done!')
