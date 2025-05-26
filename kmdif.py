import os
import fcntl
import mmap
import struct
import ctypes
import errno
import random

# --- IOCTL / Ctypes Definitions ---
_IOC_NRBITS, _IOC_TYPEBITS, _IOC_SIZEBITS, _IOC_DIRBITS = 8, 8, 14, 2
_IOC_NONE, _IOC_WRITE, _IOC_READ = 0, 1, 2
_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS

def _IOC(direction, typecode, number, size):
    return (direction << _IOC_DIRSHIFT) | (typecode << _IOC_TYPESHIFT) | \
           (number << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)
def _IO(typecode, number): return _IOC(_IOC_NONE, typecode, number, 0)

TENSTORRENT_IOCTL_MAGIC = 0xFA
TENSTORRENT_IOCTL_ALLOCATE_TLB_CMD = _IO(TENSTORRENT_IOCTL_MAGIC, 11)
TENSTORRENT_IOCTL_FREE_TLB_CMD = _IO(TENSTORRENT_IOCTL_MAGIC, 12)
TENSTORRENT_IOCTL_CONFIGURE_TLB_CMD = _IO(TENSTORRENT_IOCTL_MAGIC, 13)

TWO_MEGABYTES = (1 << 21)
# Assumed value for strict ordering, matching C code's typical initialization.
# (e.g. struct tt_noc_params_t params = {0}; params.ordering = TT_NOC_ORDERING_STRICT;)
# If TT_NOC_ORDERING_STRICT is non-zero, this value needs to be adjusted.
TT_NOC_ORDERING_STRICT_VAL = 0

class TenstorrentAllocateTlbIn(ctypes.Structure): _fields_ = [("size", ctypes.c_uint64), ("reserved", ctypes.c_uint64)]
class TenstorrentAllocateTlbOut(ctypes.Structure): _fields_ = [("id", ctypes.c_uint32), ("reserved0", ctypes.c_uint32), ("mmap_offset_uc", ctypes.c_uint64), ("mmap_offset_wc", ctypes.c_uint64), ("reserved1", ctypes.c_uint64)]
class TenstorrentAllocateTlb(ctypes.Structure): _fields_ = [("in_", TenstorrentAllocateTlbIn), ("out", TenstorrentAllocateTlbOut)]
class TenstorrentFreeTlbIn(ctypes.Structure): _fields_ = [("id", ctypes.c_uint32)]
class TenstorrentFreeTlbOut(ctypes.Structure): _fields_ = []
class TenstorrentFreeTlb(ctypes.Structure): _fields_ = [("in_", TenstorrentFreeTlbIn), ("out", TenstorrentFreeTlbOut)]
class TenstorrentNocTlbConfig(ctypes.Structure): _fields_ = [("addr", ctypes.c_uint64), ("x_end", ctypes.c_uint16), ("y_end", ctypes.c_uint16), ("x_start", ctypes.c_uint16), ("y_start", ctypes.c_uint16), ("noc", ctypes.c_uint8), ("mcast", ctypes.c_uint8), ("ordering", ctypes.c_uint8), ("linked", ctypes.c_uint8), ("static_vc", ctypes.c_uint8), ("reserved0", ctypes.c_uint8 * 3), ("reserved1", ctypes.c_uint32 * 2)]
class TenstorrentConfigureTlbIn(ctypes.Structure): _fields_ = [("id", ctypes.c_uint32), ("config", TenstorrentNocTlbConfig)]
class TenstorrentConfigureTlbOut(ctypes.Structure): _fields_ = [("reserved", ctypes.c_uint64)]
class TenstorrentConfigureTlb(ctypes.Structure): _fields_ = [("in_", TenstorrentConfigureTlbIn), ("out", TenstorrentConfigureTlbOut)]

def _min(a, b): return a if a < b else b

class _ManagedTlb:
    """Internal context manager for TLB allocation, mapping, and cleanup."""
    def __init__(self, device_fd: int,
                 alloc_tlb_func, # Callable: (size) -> TenstorrentAllocateTlbOut
                 free_tlb_func,  # Callable: (tlb_id) -> None
                 tlb_alloc_size: int, use_wc_mapping: bool):
        self.device_fd = device_fd
        self._alloc_tlb_func = alloc_tlb_func
        self._free_tlb_func = free_tlb_func
        self.tlb_alloc_size = tlb_alloc_size
        self.use_wc_mapping = use_wc_mapping
        
        self.tlb_id: int = -1
        self.mapping: mmap.mmap = None
        self.mmap_offset_uc: int = 0
        self.mmap_offset_wc: int = 0

    def __enter__(self):
        tlb_out_struct = self._alloc_tlb_func(self.tlb_alloc_size)
        self.tlb_id = tlb_out_struct.id
        self.mmap_offset_uc = tlb_out_struct.mmap_offset_uc
        self.mmap_offset_wc = tlb_out_struct.mmap_offset_wc
        
        mmap_offset_to_use = self.mmap_offset_wc if self.use_wc_mapping else self.mmap_offset_uc
        
        self.mapping = mmap.mmap(self.device_fd, self.tlb_alloc_size,
                                 mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE,
                                 offset=mmap_offset_to_use)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.mapping:
            try: self.mapping.close()
            except Exception: pass # Best effort during cleanup
        if self.tlb_id != -1:
            try: self._free_tlb_func(self.tlb_id)
            except Exception: pass # Best effort during cleanup

class TenstorrentDevice:
    def __init__(self, chardev_path: str):
        self.chardev_path = chardev_path
        self.fd = -1
        self.default_tlb_window_size = TWO_MEGABYTES

    def open(self):
        if self.fd != -1: return
        try:
            self.fd = os.open(self.chardev_path, os.O_RDWR | getattr(os, 'O_CLOEXEC', 0))
        except OSError as e:
            raise OSError(e.errno, f"Failed to open {self.chardev_path}: {os.strerror(e.errno)}", self.chardev_path) from e

    def close(self):
        if self.fd != -1:
            try: os.close(self.fd)
            except OSError: pass # Suppress close errors during cleanup
            finally: self.fd = -1

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def _ensure_open(self):
        if self.fd == -1:
            raise RuntimeError(f"Device {self.chardev_path} is not open.")

    # --- Raw IOCTL wrappers ---
    def _ioctl_allocate_tlb(self, size: int) -> TenstorrentAllocateTlbOut:
        self._ensure_open()
        alloc_struct = TenstorrentAllocateTlb()
        alloc_struct.in_.size = size
        fcntl.ioctl(self.fd, TENSTORRENT_IOCTL_ALLOCATE_TLB_CMD, alloc_struct)
        return alloc_struct.out

    def _ioctl_free_tlb(self, tlb_id: int):
        self._ensure_open()
        free_struct = TenstorrentFreeTlb()
        free_struct.in_.id = tlb_id
        fcntl.ioctl(self.fd, TENSTORRENT_IOCTL_FREE_TLB_CMD, free_struct)

    def _ioctl_configure_tlb(self, tlb_id: int, aligned_addr: int, x: int, y: int,
                             x_start: int = 0, y_start: int = 0, noc: int = 0, mcast: int = 0,
                             ordering: int = TT_NOC_ORDERING_STRICT_VAL, linked: int = 0, static_vc: int = 0):
        self._ensure_open()
        conf_struct = TenstorrentConfigureTlb()
        conf_struct.in_.id = tlb_id
        cfg = conf_struct.in_.config
        cfg.addr, cfg.x_end, cfg.y_end = aligned_addr, x, y
        cfg.x_start, cfg.y_start, cfg.noc, cfg.mcast = x_start, y_start, noc, mcast
        cfg.ordering, cfg.linked, cfg.static_vc = ordering, linked, static_vc
        fcntl.ioctl(self.fd, TENSTORRENT_IOCTL_CONFIGURE_TLB_CMD, conf_struct)

    # --- Public NOC access methods ---
    def noc_read32(self, noc: int, x: int, y: int, address: int) -> int:
        self._ensure_open()
        if address % 4 != 0: raise ValueError("Address must be 4-byte aligned.")

        with _ManagedTlb(self.fd, self._ioctl_allocate_tlb, self._ioctl_free_tlb,
                           self.default_tlb_window_size, use_wc_mapping=False) as mtlb: # UC mapping
            
            aligned_addr = address & ~(mtlb.tlb_alloc_size - 1)
            offset_in_tlb = address & (mtlb.tlb_alloc_size - 1)
            if offset_in_tlb + 4 > mtlb.tlb_alloc_size: # Ensure read is within bounds
                raise ValueError("Read offset + 4 exceeds TLB window size.")

            self._ioctl_configure_tlb(mtlb.tlb_id, aligned_addr, x, y, noc=noc)
            
            packed_val = mtlb.mapping[offset_in_tlb : offset_in_tlb + 4]
            return struct.unpack('<I', packed_val)[0]

    def noc_write32(self, noc: int, x: int, y: int, address: int, data: int):
        self._ensure_open()
        if address % 4 != 0: raise ValueError("Address must be 4-byte aligned.")
        if not (0 <= data <= 0xFFFFFFFF): raise ValueError("Data must be a 32-bit unsigned integer.")

        with _ManagedTlb(self.fd, self._ioctl_allocate_tlb, self._ioctl_free_tlb,
                           self.default_tlb_window_size, use_wc_mapping=False) as mtlb: # UC mapping
            
            aligned_addr = address & ~(mtlb.tlb_alloc_size - 1)
            offset_in_tlb = address & (mtlb.tlb_alloc_size - 1)
            if offset_in_tlb + 4 > mtlb.tlb_alloc_size: # Ensure write is within bounds
                raise ValueError("Write offset + 4 exceeds TLB window size.")

            self._ioctl_configure_tlb(mtlb.tlb_id, aligned_addr, x, y, noc=noc)
            
            mtlb.mapping[offset_in_tlb : offset_in_tlb + 4] = struct.pack('<I', data)

    def noc_read(self, noc: int, x: int, y: int, address: int, size_to_read: int) -> bytearray:
        self._ensure_open()
        if address % 4 != 0: raise ValueError("Start address must be 4-byte aligned.")
        if size_to_read < 0: raise ValueError("Size to read cannot be negative.")
        if size_to_read % 4 != 0: raise ValueError("Size to read must be a multiple of 4 bytes.")
        if size_to_read == 0: return bytearray()

        result_buffer = bytearray(size_to_read)
        current_address = address
        bytes_read = 0

        # Use WC mapping for block transfers
        with _ManagedTlb(self.fd, self._ioctl_allocate_tlb, self._ioctl_free_tlb,
                           self.default_tlb_window_size, use_wc_mapping=True) as mtlb:
            
            while bytes_read < size_to_read:
                aligned_chip_addr = current_address & ~(mtlb.tlb_alloc_size - 1)
                offset_in_tlb = current_address & (mtlb.tlb_alloc_size - 1)
                
                chunk_size = _min(size_to_read - bytes_read, mtlb.tlb_alloc_size - offset_in_tlb)
                if chunk_size <= 0 : break # Should complete loop correctly

                self._ioctl_configure_tlb(mtlb.tlb_id, aligned_chip_addr, x, y, noc=noc)
                
                chunk_data = mtlb.mapping[offset_in_tlb : offset_in_tlb + chunk_size]
                result_buffer[bytes_read : bytes_read + chunk_size] = chunk_data
                
                bytes_read += chunk_size
                current_address += chunk_size
        
        return result_buffer

    def noc_write(self, noc: int, x: int, y: int, address: int, data_buffer: bytes):
        self._ensure_open()
        if not isinstance(data_buffer, (bytes, bytearray)):
            raise TypeError("data_buffer must be bytes or bytearray.")
        size_to_write = len(data_buffer)

        if address % 4 != 0: raise ValueError("Start address must be 4-byte aligned.")
        if size_to_write < 0: raise ValueError("Data buffer size cannot be negative.") # len() is >=0
        if size_to_write % 4 != 0: raise ValueError("Data buffer size must be a multiple of 4 bytes.")
        if size_to_write == 0: return

        current_address = address
        bytes_written = 0

        # Use WC mapping for block transfers
        with _ManagedTlb(self.fd, self._ioctl_allocate_tlb, self._ioctl_free_tlb,
                           self.default_tlb_window_size, use_wc_mapping=True) as mtlb:
            
            while bytes_written < size_to_write:
                aligned_chip_addr = current_address & ~(mtlb.tlb_alloc_size - 1)
                offset_in_tlb = current_address & (mtlb.tlb_alloc_size - 1)

                chunk_size = _min(size_to_write - bytes_written, mtlb.tlb_alloc_size - offset_in_tlb)
                if chunk_size <= 0 : break

                self._ioctl_configure_tlb(mtlb.tlb_id, aligned_chip_addr, x, y, noc=noc)
                
                # Create a memoryview for efficient slicing without copying the source buffer
                data_view = memoryview(data_buffer)[bytes_written : bytes_written + chunk_size]
                mtlb.mapping[offset_in_tlb : offset_in_tlb + chunk_size] = data_view
                
                bytes_written += chunk_size
                current_address += chunk_size

if __name__ == '__main__':
    char_dev_path = "/dev/tenstorrent/0" 

    print(f"Attempting to use Tenstorrent device: {char_dev_path}")
    try:
        with TenstorrentDevice(char_dev_path) as device:
            print(f"Device {char_dev_path} opened successfully.")

            # Example: Write and Read a 32-bit value
            target_x, target_y = 8, 3
            test_addr = 0x400030000000
            write_val = 0xBEEFCAFE
          
            print(f"Writing {write_val:#x} to addr {test_addr:#x} at ({target_x},{target_y})")
            device.noc_write32(0, target_x, target_y, test_addr, write_val)
          
            read_val = device.noc_read32(0, target_x, target_y, test_addr)
            print(f"Read back {read_val:#x} from addr {test_addr:#x}")

            if read_val == write_val:
                print("noc_write32/noc_read32 test PASSED!")
            else:
                print(f"noc_write32/noc_read32 test FAILED! Expected {write_val:#x}, got {read_val:#x}")

            # Example: Write and Read a block of data
            block_addr = 0x400040000008
            # Create a buffer with a pattern (16 bytes)
            # original_block_data = bytes([i % 256 for i in range(1 << 28)]) 
            original_block_data = bytes([random.randint(0, 255) for _ in range(1 << 28)])
            print(f"Writing block of {len(original_block_data)} bytes to {block_addr:#x}")
            device.noc_write(0, target_x, target_y, block_addr, original_block_data)

            read_block_data = device.noc_read(0, target_x, target_y, block_addr, len(original_block_data))
            print(f"Read back block of {len(read_block_data)}")

            if read_block_data == original_block_data:
                print("noc_write/noc_read (block) test PASSED!")
            else:
                print("noc_write/noc_read (block) test FAILED!")


    except FileNotFoundError:
        print(f"Error: Device '{char_dev_path}' not found.")
    except PermissionError:
        print(f"Error: Permission denied for '{char_dev_path}'.")
    except OSError as e:
        print(f"An OS error occurred: {e}")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
