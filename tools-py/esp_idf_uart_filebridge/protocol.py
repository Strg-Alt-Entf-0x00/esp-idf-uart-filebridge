import serial
import serial.tools.list_ports
import struct
import binascii
import time
import logging
import threading
import sys
from cobs import cobs  # Professional COBS implementation
from functools import wraps

logger = logging.getLogger(__name__)

# Protocol Constants
PROTO_MAGIC_0 = 0xF1
PROTO_MAGIC_1 = 0x1E
PROTO_MAX_PAYLOAD = 32768
PROTO_FILE_CHUNK_SIZE = 8192  # Increased for optimal throughput with flow control
PROTO_HEADER_SIZE = 9
PROTO_CRC_SIZE = 4

# Command IDs (must match C++)
CMD_HELLO           = 0x01
CMD_DEVICE_INFO     = 0x02
CMD_REBOOT          = 0x03
CMD_SET_BAUDRATE    = 0x04
CMD_ACK             = 0x10
CMD_NACK            = 0x11
CMD_PROGRESS        = 0x12
CMD_LIST_BEGIN      = 0x20
CMD_LIST_ENTRY      = 0x21
CMD_LIST_END        = 0x22
CMD_STAT            = 0x30
CMD_GET_FILE_BEGIN  = 0x31
CMD_GET_FILE_DATA   = 0x32
CMD_GET_FILE_END    = 0x33
CMD_PUT_FILE_BEGIN  = 0x34
CMD_PUT_FILE_DATA   = 0x35
CMD_PUT_FILE_END    = 0x36
CMD_PUT_FILE_ABORT  = 0x37
CMD_DELETE          = 0x40
CMD_RENAME          = 0x41
CMD_MKDIR           = 0x42
CMD_COPY            = 0x43
CMD_SPACE_INFO      = 0x50
CMD_HASH_FILE       = 0x51
CMD_STORAGE_LIST    = 0x52  # v2.0: List all storage backends
CMD_STORAGE_INFO    = 0x53  # v2.0: Get backend details
CMD_FORMAT_FS       = 0x60

# Flags
FLAG_COMPRESSED     = 0x01  # v2.0: Data is compressed

ERR_FILE_NOT_FOUND = 0x12
ERR_FILE_EXISTS = 0x13

# Multiplexing packet types
MUX_TYPE_CONSOLE = 0x01
MUX_TYPE_PROTOCOL = 0x02

# Maximum frame size (industrial-grade: match ESP32 side)
MUX_MAX_FRAME_SIZE = 33000  # Safe upper bound for COBS encoded frame

def mux_encode_frame(pkt_type, payload):
    """
    Encode multiplexed frame with COBS (using professional library)
    Returns: encoded frame with delimiter (ready to send)
    """
    # Build raw frame: [TYPE][LENGTH][PAYLOAD][CRC16]
    raw_frame = bytearray()
    raw_frame.append(pkt_type)
    raw_frame.extend(struct.pack('<H', len(payload)))
    raw_frame.extend(payload)
    
    # Calculate CRC16
    crc = crc16_ccitt(bytes(raw_frame))
    raw_frame.extend(struct.pack('<H', crc))
    
    # COBS encode (professional implementation)
    encoded = cobs.encode(bytes(raw_frame))
    
    # Add delimiter
    return encoded + b'\x00'

def mux_decode_frame(encoded_frame):
    """
    Decode multiplexed frame (using professional COBS library)
    Returns: (packet_type, payload) or None if invalid
    """
    try:
        # COBS decode (professional implementation)
        raw = cobs.decode(bytes(encoded_frame))
        logger.debug(f"COBS decoded: {len(raw)} bytes -> {raw.hex()}")
    except Exception as e:
        logger.debug(f"COBS decode failed: {e}")
        return None
    
    if len(raw) < 5:  # Type(1) + Length(2) + CRC16(2)
        logger.debug(f"Frame too short: {len(raw)} bytes")
        return None
    
    # Parse header
    pkt_type = raw[0]
    payload_len = struct.unpack('<H', raw[1:3])[0]
    
    expected_len = 3 + payload_len + 2
    if len(raw) != expected_len:
        logger.debug(f"Length mismatch: got {len(raw)}, expected {expected_len} (payload_len={payload_len})")
        return None
    
    # Verify CRC16
    received_crc = struct.unpack('<H', raw[-2:])[0]
    calculated_crc = crc16_ccitt(raw[:-2])
    
    logger.debug(f"CRC check: received=0x{received_crc:04X}, calculated=0x{calculated_crc:04X}")
    
    if received_crc != calculated_crc:
        logger.warning(f"MUX CRC mismatch: received=0x{received_crc:04X}, calculated=0x{calculated_crc:04X}")
        return None
    
    # Extract payload
    payload = raw[3:3+payload_len] if payload_len > 0 else b''
    
    logger.debug(f"MUX frame decoded: type=0x{pkt_type:02X}, payload={len(payload)} bytes")
    return (pkt_type, payload)

def crc16_ccitt(data):
    """
    Calculate CRC16-CCITT checksum
    Polynomial: 0x1021, Init: 0xFFFF
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

def with_lock(f):
    @wraps(f)
    def wrapper(self, *args, **kwargs):
        with self._lock:
            return f(self, *args, **kwargs)
    return wrapper


class ESP32ProtocolError(Exception):
    def __init__(self, message, error_code=0, context_value=0, heap_free=0):
        super().__init__(message)
        self.error_code = error_code
        self.context_value = context_value
        self.heap_free = heap_free


class FileEntry:
    def __init__(self, data: bytes):
        # struct FileEntry: char name[256], uint64_t size, uint32_t timestamp, uint8_t is_dir, uint8_t attr, uint8_t reserved[2]
        unpacked = struct.unpack('<256sQIBB2s', data)
        self.name = unpacked[0].split(b'\0', 1)[0].decode('utf-8')
        self.size = unpacked[1]
        self.timestamp = unpacked[2]
        self.is_directory = bool(unpacked[3])
        self.attributes = unpacked[4]

class FileStat:
    def __init__(self, data: bytes):
        # struct FileStat: uint64_t size, uint32_t timestamp, uint8_t is_dir, uint8_t attr, uint8_t reserved[2]
        unpacked = struct.unpack('<QIBB2s', data)
        self.size = unpacked[0]
        self.timestamp = unpacked[1]
        self.is_directory = bool(unpacked[2])
        self.attributes = unpacked[3]


class StorageBackendInfo:
    """Storage backend information (v2.0 multi-backend support)."""
    
    # Storage types (must match C++ StorageType enum in storage_backend.h)
    TYPE_SD_FAT = 1
    TYPE_FLASH_FAT = 2
    TYPE_LITTLEFS = 3
    TYPE_SPIFFS = 4
    TYPE_CUSTOM = 255
    
    TYPE_NAMES = {
        TYPE_SD_FAT: "SD/FAT",
        TYPE_FLASH_FAT: "Flash/FAT",
        TYPE_LITTLEFS: "LittleFS",
        TYPE_SPIFFS: "SPIFFS",
        TYPE_CUSTOM: "Custom"
    }
    
    # Capability flags (must match C++ StorageCapability enum in storage_backend.h)
    CAP_NONE              = 0
    CAP_READ              = 1 << 0
    CAP_WRITE             = 1 << 1
    CAP_DIRECTORIES       = 1 << 2
    CAP_RECURSIVE_DELETE  = 1 << 3
    CAP_RENAME            = 1 << 4
    CAP_COPY              = 1 << 5
    CAP_HASH              = 1 << 6
    CAP_FORMAT            = 1 << 7
    CAP_TIMESTAMPS        = 1 << 8
    CAP_LARGE_FILES       = 1 << 9
    CAP_COMPRESSION       = 1 << 10
    CAP_WEAR_LEVELING     = 1 << 11
    CAP_ALL               = 0xFFFFFFFF
    
    # Backward compatibility aliases
    CAP_FILE_READ         = CAP_READ
    CAP_FILE_WRITE        = CAP_WRITE
    CAP_FILE_DELETE       = CAP_RECURSIVE_DELETE
    CAP_FILE_RENAME       = CAP_RENAME
    CAP_FILE_COPY         = CAP_COPY
    CAP_FILE_HASH         = CAP_HASH
    
    def __init__(self, data: bytes):
        # struct StorageBackendInfo: char mount_point[64], uint8_t type, char type_name[32],
        # uint32_t capabilities, uint8_t mounted, uint64_t total_bytes, uint64_t free_bytes
        # Exactly 118 bytes packed (<64sB32sIBQQ)
        unpacked = struct.unpack('<64sB32sIBQQ', data[:118])
        self.mount_point = unpacked[0].split(b'\0', 1)[0].decode('utf-8')
        self.type = unpacked[1]
        self.type_name = unpacked[2].split(b'\0', 1)[0].decode('utf-8')
        self.capabilities = unpacked[3]
        self.mounted = bool(unpacked[4])
        self.total_bytes = unpacked[5]
        self.free_bytes = unpacked[6]
    
    def has_capability(self, cap: int) -> bool:
        """Check if backend has specific capability."""
        return (self.capabilities & cap) != 0
    
    def get_used_bytes(self) -> int:
        """Get used bytes."""
        return self.total_bytes - self.free_bytes if self.total_bytes > 0 else 0
    
    def get_usage_percentage(self) -> float:
        """Get usage percentage (0-100)."""
        if self.total_bytes == 0:
            return 0.0
        return (self.get_used_bytes() / self.total_bytes) * 100.0
    
    def __str__(self):
        status = "mounted" if self.mounted else "unmounted"
        return (f"{self.mount_point} [{self.type_name}] {status}: "
                f"{self.free_bytes // (1024*1024)} MB free of "
                f"{self.total_bytes // (1024*1024)} MB total")


class DeviceInfo:
    def __init__(self, data: bytes):
        # char device_name[32], uint8_t fw_maj, min, patch, hw_rev, uint32_t flash_sz, flash_fr, psram_sz, psram_fr,
        # uint8_t sd_present, uint64_t sd_size, sd_free, uint8_t sd_type, char sd_fs_type[16],
        # uint32_t max_payload_size, uint32_t optimal_chunk_size
        
        # Determine format based on payload length to maintain backwards compatibility
        if len(data) >= 94:
            unpacked = struct.unpack('<32sBBBBIIIIBQQB16sII', data[:94])
            self.max_payload_size = unpacked[14]
            self.optimal_chunk_size = unpacked[15]
        else:
            unpacked = struct.unpack('<32sBBBBIIIIBQQB16s', data[:86])
            self.max_payload_size = PROTO_MAX_PAYLOAD
            self.optimal_chunk_size = PROTO_FILE_CHUNK_SIZE
            
        self.device_name = unpacked[0].split(b'\0', 1)[0].decode('utf-8')
        self.fw_version = f"{unpacked[1]}.{unpacked[2]}.{unpacked[3]}"
        self.sd_present = bool(unpacked[9])
        self.sd_size = unpacked[10]
        self.sd_free = unpacked[11]
        self.sd_type = unpacked[12]
        self.sd_fs_type = unpacked[13].split(b'\0', 1)[0].decode('utf-8')


class ESP32Protocol:
    def __init__(self):
        self.ser = None
        self.sequence = 0
        self._rx_sequence = None
        self._lock = threading.RLock()
        self.chunk_size = PROTO_FILE_CHUNK_SIZE
        self.max_payload = PROTO_MAX_PAYLOAD

    @with_lock
    def connect(self, port_name: str, baud_rate: int = 3000000, rtscts: bool = False, max_boot_wait_sec: float = 3.0) -> bool:
        """
        Industrial-grade connection with automatic boot recovery.
        
        Args:
            port_name: Serial port (e.g., COM6, /dev/ttyUSB0)
            baud_rate: Baud rate (default: 3000000)
            rtscts: Enable hardware flow control (default: False)
            max_boot_wait_sec: Maximum time to wait for ESP32 boot (default: 3.0)
        
        Returns:
            True if connection successful, False otherwise
        """
        modes_to_try = [rtscts]
        if rtscts:
            modes_to_try.append(False)

        for mode in modes_to_try:
            for attempt in range(3):
                try:
                    if self.ser and self.ser.is_open:
                        self.disconnect()

                    if attempt > 0:
                        time.sleep(0.3)

                    # INDUSTRIAL-GRADE: Open serial port without triggering reset
                    # Set DTR=False BEFORE opening to prevent ESP32 reset on some systems
                    self.ser = serial.Serial()
                    self.ser.port = port_name
                    self.ser.baudrate = baud_rate
                    self.ser.timeout = 0.5
                    self.ser.rtscts = mode
                    self.ser.dsrdtr = False
                    self.ser.xonxoff = True
                    self.ser.write_timeout = 2.0 if mode else 5.0
                    
                    # Critical: Set DTR/RTS low BEFORE opening
                    self.ser.dtr = False
                    self.ser.rts = False
                    
                    # Now open the port
                    self.ser.open()
                    
                    # Ensure they stayed low after opening
                    try:
                        self.ser.dtr = False
                        self.ser.rts = False
                    except Exception:
                        pass
                    
                    self.sequence = 0
                    self._rx_sequence = None
                    self.ser.reset_input_buffer()
                    
                    # INDUSTRIAL-GRADE: Smart boot detection and recovery
                    boot_detected = self._drain_boot_noise(max_wait_ms=int(max_boot_wait_sec * 1000))
                    
                    # Try sending HELLO with boot recovery
                    hello_attempts = 10 if boot_detected else 3
                    for hello_try in range(hello_attempts):
                        if self.send_hello():
                            logger.info(f"Connected to {port_name} at {baud_rate} baud")
                            
                            # INDUSTRIAL-GRADE: After boot, wait for BOOT_READY to ensure ESP32 is stable
                            if boot_detected:
                                if self._wait_for_boot_ready(timeout_sec=3.0):
                                    logger.info("BOOT_READY received - ESP32 stable and ready")
                                    return True
                                else:
                                    # No BOOT_READY - ESP32 may have reset again, retry
                                    logger.warning(f"No BOOT_READY after HELLO attempt {hello_try+1}, retrying...")
                                    time.sleep(0.5)
                                    continue
                            
                            return True
                        
                        wait_time = 0.5 if boot_detected else 0.2
                        time.sleep(wait_time)

                    self.disconnect()
                except Exception as e:
                    logger.debug(f"Connect attempt {attempt+1} (rtscts={mode}) to {port_name} failed: {e}")
                    self.disconnect()

        logger.error(f"Failed to connect to {port_name} @ {baud_rate} baud")
        return False

    @with_lock
    def auto_detect(self) -> bool:
        ports = serial.tools.list_ports.comports()
        for port in ports:
            logger.info(f"Probing {port.device}...")
            if self.connect(port.device):
                info = self.get_device_info()
                if info:
                    logger.info(f"Found {info.device_name} on {port.device}!")
                    self.chunk_size = info.optimal_chunk_size
                    self.max_payload = info.max_payload_size
                    logger.info(f"Negotiated payload size: {self.max_payload} bytes, chunk size: {self.chunk_size} bytes")
                    return True
                self.disconnect()
        return False

    @with_lock
    def disconnect(self):
        if self.ser and self.ser.is_open:
            self._rx_sequence = None
            self.ser.close()
        self.ser = None
        self._rx_sequence = None

    def _drain_boot_noise(self, max_wait_ms=5000) -> bool:
        """
        Drain boot messages and noise from the serial buffer.
        
        Industrial-grade implementation with boot detection.
        
        Args:
            max_wait_ms: Maximum time to wait for quiet period (default: 5000ms)
        
        Returns:
            True if boot messages were detected, False otherwise
        """
        deadline = time.monotonic() + (max_wait_ms / 1000.0)
        self.ser.timeout = 0.1
        consecutive_quiet = 0
        total_drained = 0
        boot_detected = False

        logger.debug("Draining boot noise...")
        while time.monotonic() < deadline:
            data = self.ser.read(64)
            if data:
                total_drained += len(data)
                consecutive_quiet = 0
                
                # Detect ESP32 boot messages
                data_str = data.decode('utf-8', errors='ignore')
                if any(marker in data_str for marker in [
                    'rst:', 'boot:', 'ESP-ROM:', 'ESP32', 'Brownout',
                    'uart0_multiplex', 'Filebridge Ready', 'Registered backend'
                ]):
                    boot_detected = True
                    logger.debug(f"Boot messages detected")
            else:
                consecutive_quiet += 1
                if consecutive_quiet >= 3:
                    break

        if boot_detected:
            logger.info(f"ESP32 boot detected - drained {total_drained} bytes, waiting for stability")
        elif total_drained > 0:
            logger.debug(f"Drained {total_drained} bytes of noise")
        
        self.ser.timeout = 5.0  # Restore normal timeout
        return boot_detected

    def _wait_for_boot_ready(self, timeout_sec: float = 5.0) -> bool:
        """
        Wait for CMD_BOOT_READY signal from ESP32.
        
        Industrial-grade: Instead of guessing with timers, wait for explicit signal.
        
        Args:
            timeout_sec: Maximum time to wait (default: 5.0s)
        
        Returns:
            True if BOOT_READY received, False on timeout
        """
        deadline = time.monotonic() + timeout_sec
        
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            try:
                cmd, _ = self._receive_frame(timeout_sec=max(0.1, remaining))
                if cmd == 0x05:  # CMD_BOOT_READY
                    return True
                # Ignore other frames during boot
                logger.debug(f"Ignoring frame cmd=0x{cmd:02X} while waiting for BOOT_READY")
            except ESP32ProtocolError:
                # Timeout or decode error - keep trying
                pass
        
        return False

    def _calc_crc32(self, data: bytes) -> int:
        return binascii.crc32(data) & 0xFFFFFFFF

    def _deadline(self, timeout_sec: float) -> float:
        return time.monotonic() + max(timeout_sec, 0.0)

    def _send_frame(self, cmd: int, payload: bytes = b'', flags: int = 0):
        if not self.ser or not self.ser.is_open:
            raise ESP32ProtocolError("Not connected")
        if len(payload) > self.max_payload or len(payload) > 0xFFFF:
            raise ESP32ProtocolError(
                f"Payload too large: {len(payload)} bytes (max {self.max_payload})"
            )

        length = len(payload)
        # Build protocol frame
        header = struct.pack('<BBBBBHH', PROTO_MAGIC_0, PROTO_MAGIC_1, 0x10, cmd, flags, self.sequence, length)
        self.sequence = (self.sequence + 1) & 0xFFFF
        
        protocol_frame = bytearray(header)
        protocol_frame.extend(payload)
        
        # Calculate CRC
        crc = binascii.crc32(protocol_frame) & 0xFFFFFFFF
        protocol_frame.extend(struct.pack('<I', crc))
        
        # Wrap protocol frame in multiplexer (TYPE=0x02)
        mux_frame = mux_encode_frame(MUX_TYPE_PROTOCOL, bytes(protocol_frame))
        
        # Send to ESP32
        try:
            self.ser.write(mux_frame)
            self.ser.flush()
        except serial.SerialTimeoutException as e:
            raise ESP32ProtocolError(f"Serial write timeout: {e}")

    def _receive_frame(self, expected_cmd=None, timeout_sec=5.0):
        """
        Industrial-grade frame receiver with graceful degradation and buffer overflow protection.
        
        Receives and demultiplexes frame from ESP32 with automatic console output handling.
        """
        if not self.ser or not self.ser.is_open:
            raise ESP32ProtocolError("Not connected")

        deadline = self._deadline(timeout_sec)
        self.ser.timeout = min(max(timeout_sec, 0.05), 0.5)
        frame_buffer = bytearray()

        while time.monotonic() < deadline:
            # Read until 0x00 delimiter (COBS frame boundary)
            remaining = max(0.05, deadline - time.monotonic())
            
            try:
                while time.monotonic() < deadline:
                    byte = self.ser.read(1)
                    if not byte:
                        # Timeout waiting for data
                        if not frame_buffer:
                            continue  # No data yet, keep waiting
                        else:
                            raise ESP32ProtocolError("Timeout mid-frame")
                    
                    if byte[0] == 0x00:
                        # Frame delimiter found
                        break
                    
                    frame_buffer.append(byte[0])
                    
                    # INDUSTRIAL-GRADE: Buffer overflow protection
                    # Prevent memory exhaustion from corrupted stream
                    if len(frame_buffer) > MUX_MAX_FRAME_SIZE:
                        logger.warning(f"Frame buffer overflow ({len(frame_buffer)} bytes) - resetting stream")
                        frame_buffer.clear()
                        continue
                
                if not frame_buffer:
                    # Empty frame, continue
                    continue
                
                # Decode multiplexed frame
                result = mux_decode_frame(bytes(frame_buffer))
                frame_buffer.clear()  # Clear for next frame
                
                if result is None:
                    # CRC error or invalid frame - resync
                    # INDUSTRIAL-GRADE: Don't crash on boot noise or corrupted frames
                    logger.debug(f"MUX frame decode failed (len={len(frame_buffer)}) - resyncing gracefully")
                    continue
                
                pkt_type, payload = result
                
                if pkt_type == MUX_TYPE_CONSOLE:
                    # Console output from ESP32 - print to stdout
                    try:
                        text = payload.decode('utf-8', errors='replace')
                        sys.stdout.write(text)
                        sys.stdout.flush()
                    except Exception as e:
                        logger.debug(f"Failed to decode console text: {e}")
                    # Continue reading - we want protocol frames
                    continue
                
                elif pkt_type == MUX_TYPE_PROTOCOL:
                    # Binary protocol frame - parse it
                    return self._parse_protocol_frame(payload, expected_cmd)
                
                else:
                    logger.warning(f"Unknown MUX packet type: 0x{pkt_type:02X}")
                    continue
                    
            except serial.SerialTimeoutException:
                raise ESP32ProtocolError("Timeout reading frame")
        
        raise ESP32ProtocolError("Timeout waiting for protocol frame")
    
    def _parse_protocol_frame(self, payload, expected_cmd=None):
        """Parse protocol frame from multiplexed payload."""
        if len(payload) < PROTO_HEADER_SIZE:
            raise ESP32ProtocolError("Protocol frame too short")
        
        # Parse header
        header = payload[:PROTO_HEADER_SIZE]
        magic0, magic1, version, cmd, flags, seq, length = struct.unpack('<BBBBBHH', header)
        
        # Verify magic
        if magic0 != PROTO_MAGIC_0 or magic1 != PROTO_MAGIC_1:
            raise ESP32ProtocolError(f"Invalid magic: 0x{magic0:02X} 0x{magic1:02X}")
        
        # Extract payload
        frame_payload = payload[PROTO_HEADER_SIZE:PROTO_HEADER_SIZE + length]
        if len(frame_payload) != length:
            raise ESP32ProtocolError(f"Incomplete payload (got {len(frame_payload)}, expected {length})")
        
        # Verify CRC32
        crc_start = PROTO_HEADER_SIZE + length
        if len(payload) < crc_start + PROTO_CRC_SIZE:
            raise ESP32ProtocolError("CRC missing")
        
        received_crc = struct.unpack('<I', payload[crc_start:crc_start + PROTO_CRC_SIZE])[0]
        full_frame = payload[:crc_start]
        calc_crc = self._calc_crc32(full_frame)
        
        if calc_crc != received_crc:
            raise ESP32ProtocolError("CRC mismatch")

        # Verify sequence
        if self._rx_sequence is None:
            self._rx_sequence = seq
        elif seq != self._rx_sequence:
            raise ESP32ProtocolError(
                f"Sequence mismatch: expected {self._rx_sequence}, got {seq}"
            )
        self._rx_sequence = (self._rx_sequence + 1) & 0xFFFF

        # Check expected command
        if expected_cmd is not None and cmd != expected_cmd:
            if cmd == CMD_NACK:
                raise ESP32ProtocolError("Device returned NACK")
            raise ESP32ProtocolError(f"Unexpected command: {cmd}")

        return cmd, frame_payload

    def _wait_ack(self, timeout_sec=2.0) -> bool:
        deadline = self._deadline(timeout_sec)
        while time.monotonic() < deadline:
            remaining = max(0.05, deadline - time.monotonic())
            try:
                cmd, _ = self._receive_frame(timeout_sec=remaining)
            except ESP32ProtocolError as e:
                logger.debug(f"_wait_ack error: {e}")
                return False
            if cmd == CMD_PROGRESS:
                continue
            if cmd == CMD_NACK:
                return False
            return cmd == CMD_ACK
        return False

    def _wait_ack_with_error(self, timeout_sec=2.0) -> tuple[bool, int, str]:
        """Wait for ACK with extended error information.
        Returns: (success, error_code, error_message)
        """
        deadline = self._deadline(timeout_sec)
        while time.monotonic() < deadline:
            remaining = max(0.05, deadline - time.monotonic())
            try:
                cmd, payload = self._receive_frame(timeout_sec=remaining)
            except ESP32ProtocolError as e:
                logger.debug(f"_wait_ack_with_error error: {e}")
                return False, 0, str(e)
            if cmd == CMD_PROGRESS:
                continue
            if cmd == CMD_NACK:
                # Try to parse extended NACK (struct size: 1 + 128 + 8 + 4 + 4 = 145 bytes)
                if len(payload) >= 145:
                    # Extended NACK with context
                    error_code = payload[0]
                    reason = payload[1:129].split(b'\0', 1)[0].decode('utf-8', errors='replace')
                    context_value = struct.unpack('<Q', payload[129:137])[0]
                    heap_free = struct.unpack('<I', payload[137:141])[0]
                    
                    # Build detailed error message
                    if reason:
                        msg = reason
                    else:
                        msg = f"NACK error code {error_code}"
                    
                    # Add context information
                    if error_code == 0x15:  # ERR_DISK_FULL
                        msg += f" (SD free: {context_value / (1024*1024):.1f} MB)"
                    elif error_code == 0x06:  # ERR_OUT_OF_MEMORY
                        msg += f" (Heap free: {context_value} bytes)"
                    elif error_code == 0x18:  # ERR_PATH_TOO_LONG
                        msg += f" (Path length: {context_value})"
                    
                    msg += f" [Heap: {heap_free / 1024:.1f} KB]"
                    
                    return False, error_code, msg
                elif len(payload) >= 1:
                    # Legacy simple NACK
                    error_code = payload[0]
                    return False, error_code, f"NACK error code {error_code}"
                else:
                    return False, 0, "NACK without error code"
            return cmd == CMD_ACK, 0, ""
        return False, 0, "Timeout waiting for ACK"

    @with_lock
    def send_hello(self) -> bool:
        if self.ser and self.ser.is_open:
            self.ser.reset_input_buffer()
        self.sequence = 0
        self._rx_sequence = None
        self._send_frame(CMD_HELLO)
        return self._wait_ack()

    @with_lock
    def get_device_info(self) -> DeviceInfo:
        self._send_frame(CMD_DEVICE_INFO)
        cmd, payload = self._receive_frame(CMD_DEVICE_INFO)
        return DeviceInfo(payload)

    @with_lock
    def get_storage_list(self):
        """Get list of all registered storage backends (v2.0).
        
        Returns:
            list: List of StorageBackendInfo objects
        """
        self._send_frame(CMD_STORAGE_LIST)
        self._wait_ack()
        
        backends = []
        while True:
            cmd, payload = self._receive_frame(timeout_sec=30.0)
            if cmd == CMD_LIST_END:
                break
            elif cmd == CMD_LIST_ENTRY:
                backends.append(StorageBackendInfo(payload))
            elif cmd == CMD_NACK:
                raise ESP32ProtocolError("NACK during storage list")
        return backends

    @with_lock
    def get_storage_info(self, mount_point: str) -> StorageBackendInfo:
        """Get detailed information for specific storage backend (v2.0).
        
        Args:
            mount_point: Mount point of backend (e.g., "/sd", "/flash", "/littlefs")
            
        Returns:
            StorageBackendInfo: Backend information
        """
        path_bytes = mount_point.encode('utf-8') + b'\0'
        self._send_frame(CMD_STORAGE_INFO, path_bytes)
        cmd, payload = self._receive_frame(CMD_STORAGE_INFO)
        return StorageBackendInfo(payload)

    @with_lock
    def list_directory(self, path: str):
        path_bytes = path.encode('utf-8') + b'\0'
        self._send_frame(CMD_LIST_BEGIN, path_bytes)
        self._wait_ack()
        
        entries = []
        while True:
            cmd, payload = self._receive_frame(timeout_sec=120.0)
            if cmd == CMD_LIST_END:
                break
            elif cmd == CMD_LIST_ENTRY:
                entries.append(FileEntry(payload))
            elif cmd == CMD_NACK:
                raise ESP32ProtocolError("NACK during list")
        return entries

    @with_lock
    def get_stat(self, path: str) -> FileStat:
        path_bytes = path.encode('utf-8') + b'\0'
        self._send_frame(CMD_STAT, path_bytes)
        cmd, payload = self._receive_frame(CMD_STAT)
        return FileStat(payload)

    @with_lock
    def read_file(self, path: str) -> bytes:
        path_bytes = path.encode('utf-8') + b'\0'
        self._send_frame(CMD_GET_FILE_BEGIN, path_bytes)

        # Read file size from ACK
        cmd, ack_payload = self._receive_frame(CMD_ACK, timeout_sec=5.0)
        if len(ack_payload) < 8:
            raise ESP32ProtocolError("Download size ACK payload too short")
        expected_size = struct.unpack('<Q', ack_payload[:8])[0]

        data = bytearray()
        while True:
            cmd, payload = self._receive_frame(timeout_sec=120.0)
            if cmd == CMD_GET_FILE_END:
                break
            elif cmd == CMD_GET_FILE_DATA:
                data.extend(payload)
            elif cmd == CMD_NACK:
                raise ESP32ProtocolError("NACK during file read")

        if len(data) != expected_size:
            raise ESP32ProtocolError(
                f"Download size mismatch: expected {expected_size}, got {len(data)}"
            )
        return bytes(data)

    @with_lock
    def write_file(self, path: str, data: bytes):
        file_size = len(data)
        path_bytes = path.encode('utf-8') + b'\0'
        
        payload = struct.pack('<Q', file_size) + path_bytes
        self._send_frame(CMD_PUT_FILE_BEGIN, payload)
        if not self._wait_ack():
            raise ESP32ProtocolError("NACK on PUT_FILE_BEGIN")
        
        offset = 0
        try:
            while offset < file_size:
                chunk = data[offset:offset + self.chunk_size]
                
                # Streaming mode: Just send the frame, no wait_ack() needed!
                # Hardware Flow Control handles pacing.
                self._send_frame(CMD_PUT_FILE_DATA, chunk)
                
                # Check for asynchronous NACKs (e.g. SD error) without blocking
                if self.ser.in_waiting > 0:
                    try:
                        async_cmd, _ = self._receive_frame(timeout_sec=0.1)
                        if async_cmd == CMD_NACK:
                            raise ESP32ProtocolError("Received async NACK during transfer")
                    except serial.SerialTimeoutException:
                        pass
                    except Exception as e:
                        if isinstance(e, ESP32ProtocolError): raise
                
                offset += len(chunk)
                
            self._send_frame(CMD_PUT_FILE_END)
            if not self._wait_ack():
                raise ESP32ProtocolError("NACK on PUT_FILE_END")
        except Exception as e:
            # Try to abort transfer
            try:
                self._send_frame(CMD_PUT_FILE_ABORT)
                self._wait_ack(timeout_sec=2.0)
            except:
                pass
            raise ESP32ProtocolError(f"Error writing file: {e}")

    @with_lock
    def begin_write_stream(self, path: str, file_size: int = 0):
        path_bytes = path.encode('utf-8') + b'\0'
        payload = struct.pack('<Q', file_size) + path_bytes
        self._send_frame(CMD_PUT_FILE_BEGIN, payload)
        
        success, error_code, error_msg = self._wait_ack_with_error(timeout_sec=10.0)
        if not success:
            raise ESP32ProtocolError(error_msg or "NACK on PUT_FILE_BEGIN", error_code)

    @with_lock
    def write_stream_data(self, chunk: bytes):
        """Write file data chunk in streaming mode.
        Hardware flow control will block ser.write() automatically if ESP32 buffers fill up.
        When hardware flow control is not available (e.g. 2-wire UART0), a small pacing delay
        prevents ring buffer overflows while the ESP32 is committing flash pages.
        """
        self._send_frame(CMD_PUT_FILE_DATA, chunk)
        if not getattr(self.ser, 'rtscts', False):
            time.sleep(0.015)
        
        # Check for asynchronous NACKs without blocking
        if self.ser.in_waiting > 0:
            try:
                cmd, _ = self._receive_frame(timeout_sec=0.1)
                if cmd == CMD_NACK:
                    raise ESP32ProtocolError("Received async NACK during streaming")
            except serial.SerialTimeoutException:
                pass
            except Exception as e:
                if isinstance(e, ESP32ProtocolError): raise

    @with_lock
    def end_write_stream(self):
        self._send_frame(CMD_PUT_FILE_END)
        success, code, msg = self._wait_ack_with_error(timeout_sec=30.0)
        if not success:
            raise ESP32ProtocolError(f"NACK on PUT_FILE_END: {msg} (code 0x{code:02X})", code)

    @with_lock
    def abort_write_stream(self):
        self._send_frame(CMD_PUT_FILE_ABORT)
        if not self._wait_ack(timeout_sec=2.0):
            raise ESP32ProtocolError("NACK on PUT_FILE_ABORT")

    @with_lock
    def delete_file(self, path: str):
        path_bytes = path.encode('utf-8') + b'\0'
        self._send_frame(CMD_DELETE, path_bytes)
        success, err_code, err_msg = self._wait_ack_with_error()
        if not success:
            message = err_msg if err_msg else f"NACK on DELETE: {err_code}"
            raise ESP32ProtocolError(message, error_code=err_code)

    @with_lock
    def mkdir(self, path: str):
        path_bytes = path.encode('utf-8') + b'\0'
        self._send_frame(CMD_MKDIR, path_bytes)
        if not self._wait_ack():
            raise ESP32ProtocolError("NACK on MKDIR")

    @with_lock
    def format_fs(self, fs_type: int):
        # fs_type: 2 = FAT32, 3 = exFAT
        payload = struct.pack('<B', fs_type)
        self._send_frame(CMD_FORMAT_FS, payload)
        # Formatting can take a long time, especially for SD cards
        success, err_code, err_msg = self._wait_ack_with_error(timeout_sec=30.0)
        if not success:
            message = err_msg if err_msg else f"NACK or Timeout on FORMAT_FS: {err_code}"
            raise ESP32ProtocolError(message, error_code=err_code)
    
    @with_lock
    def set_baudrate(self, new_baud_rate: int):
        """Switch UART baud rate dynamically.
        
        Args:
            new_baud_rate: New baud rate (9600-4000000)
            
        Raises:
            ESP32ProtocolError: If baud rate switch fails
        """
        # Validate baud rate
        if not (9600 <= new_baud_rate <= 4000000):
            raise ESP32ProtocolError(f"Invalid baud rate: {new_baud_rate} (must be 9600-4000000)")
        
        # Send SET_BAUDRATE command
        payload = struct.pack('<I', new_baud_rate)
        self._send_frame(CMD_SET_BAUDRATE, payload)
        
        # Wait for ACK (ESP32 sends ACK before switching)
        cmd, data = self._receive_frame(expected_cmd=CMD_ACK, timeout_sec=2.0)
        
        # Switch PC side baud rate
        logger.info(f"Switching from {self.ser.baudrate} to {new_baud_rate} baud...")
        self.ser.baudrate = new_baud_rate
        time.sleep(0.1)  # Let UART stabilize
        
        # Verify connection with HELLO
        if not self.send_hello():
            raise ESP32ProtocolError(f"Failed to verify connection after baud switch to {new_baud_rate}")
        
        logger.info(f"Baud rate switched successfully to {new_baud_rate}")
    
    @with_lock
    def get_file_hash(self, path: str) -> int:
        """Get CRC32 hash of file.
        
        Args:
            path: File path on ESP32
            
        Returns:
            CRC32 hash as 32-bit unsigned integer
        """
        path_bytes = path.encode('utf-8') + b'\0'
        self._send_frame(CMD_HASH_FILE, path_bytes)
        
        # Receive response (firmware sends CMD_HASH_FILE back with hash in payload)
        cmd, payload = self._receive_frame(timeout_sec=120.0)
        
        # Check for NACK
        if cmd == CMD_NACK:
            err_code = struct.unpack('<H', payload[:2])[0] if len(payload) >= 2 else 0
            raise ESP32ProtocolError(f"NACK on HASH_FILE: {err_code}", error_code=err_code)
        
        # Firmware responds with CMD_HASH_FILE and CRC32 in payload
        if cmd != CMD_HASH_FILE:
            raise ESP32ProtocolError(f"Unexpected response to HASH_FILE: CMD={cmd:02X}")
        
        # Extract CRC32 from payload
        if len(payload) < 4:
            raise ESP32ProtocolError(f"HASH_FILE response too short: {len(payload)} bytes")
        
        crc32_value = struct.unpack('<I', payload[:4])[0]
        return crc32_value
    
    @with_lock
    def rename(self, old_path: str, new_path: str):
        """Rename or move a file/directory.
        
        Args:
            old_path: Current path
            new_path: New path
        """
        old_bytes = old_path.encode('utf-8') + b'\0'
        new_bytes = new_path.encode('utf-8') + b'\0'
        payload = old_bytes + new_bytes
        self._send_frame(CMD_RENAME, payload)
        if not self._wait_ack():
            raise ESP32ProtocolError("NACK on RENAME")
    
    @with_lock
    def copy_file(self, src_path: str, dst_path: str):
        """Copy a file.
        
        Args:
            src_path: Source file path
            dst_path: Destination file path
        """
        src_bytes = src_path.encode('utf-8') + b'\0'
        dst_bytes = dst_path.encode('utf-8') + b'\0'
        payload = src_bytes + dst_bytes
        self._send_frame(CMD_COPY, payload)
        if not self._wait_ack():
            raise ESP32ProtocolError("NACK on COPY")


