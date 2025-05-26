#!/usr/bin/env python3

import ctypes
import os
import sys

TENSTORRENT_PCI_VENDOR_ID = 0x1e52
BLACKHOLE_PCI_DEVICE_ID = 0xb140
WORMHOLE_PCI_DEVICE_ID = 0x401e

class Device:
    def __init__(self, device_path: str, library_path=None):
        self._device_handle = ctypes.c_void_p()  # Initialize to null pointer

        self._load_ttio_library(library_path)
        self._setup_prototypes()
        self._open(device_path)

    # TODO: probably should figure out how to do this properly like with venv and stuff.
    def _load_ttio_library(self, library_path=None):
        if library_path:
            if not os.path.exists(library_path):
                raise OSError(f"Specified library path does not exist: {library_path}")
            try:
                self.lib = ctypes.CDLL(library_path)
                return
            except OSError as e:
                raise OSError(f"Error loading library from specified path '{library_path}': {e}")

        lib_name = 'libttio.so'
        paths_to_try = [
            os.path.join(os.getcwd(), lib_name), # 1. Current directory
            os.path.join(os.getcwd(), "../build/ttio", lib_name), # 2. My build folder LOL fix this
        ]
        try:
            script_dir = os.path.dirname(os.path.abspath(__file__))
            paths_to_try.append(os.path.join(script_dir, lib_name)) # 2. Script directory
        except NameError: # __file__ might not be defined (e.g. in interactive interpreter)
            pass

        for path in paths_to_try:
            try:
                self.lib = ctypes.CDLL(path)
                return
            except OSError:
                pass

        # 3. Standard library paths (ctypes will search LD_LIBRARY_PATH, PATH, etc.)
        try:
            self.lib = ctypes.CDLL(lib_name)
            return
        except OSError as e:
            raise OSError(
                f"Could not load the ttio library '{lib_name}'. "
                f"Ensure it is compiled and in your system's library path "
                f"(e.g., LD_LIBRARY_PATH on Linux, PATH on Windows), "
                f"the script's directory, or provide the full path via 'library_path'. "
                f"Original error: {e}"
            )

    def _setup_prototypes(self):
        """Defines ctypes prototypes for the C functions from ttio.h."""

        # Ensure lib is loaded
        if not self.lib:
            raise RuntimeError("Library not loaded. Cannot setup prototypes.")
        
        # int32_t tt_device_open(const char* chardev_path, tt_device_t** out_device);
        self.lib.tt_device_open.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self.lib.tt_device_open.restype = ctypes.c_int32

        # int32_t tt_device_close(tt_device_t* device);
        self.lib.tt_device_close.argtypes = [ctypes.c_void_p]
        self.lib.tt_device_close.restype = ctypes.c_int32

        # int32_t tt_noc_write32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t value);
        self.lib.tt_noc_write32.argtypes = [
            ctypes.c_void_p,  # device (tt_device_t*)
            ctypes.c_uint16,  # x
            ctypes.c_uint16,  # y
            ctypes.c_uint64,  # addr
            ctypes.c_uint32   # value
        ]
        self.lib.tt_noc_write32.restype = ctypes.c_int32

        # int32_t tt_noc_read32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t* value);
        self.lib.tt_noc_read32.argtypes = [
            ctypes.c_void_p,  # device (tt_device_t*)
            ctypes.c_uint16,  # x
            ctypes.c_uint16,  # y
            ctypes.c_uint64,  # addr
            ctypes.POINTER(ctypes.c_uint32)  # out_value (uint32_t*)
        ]
        self.lib.tt_noc_read32.restype = ctypes.c_int32

        # int32_t tt_noc_read(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size);
        self.lib.tt_noc_read.argtypes = [
            ctypes.c_void_p,  # device (tt_device_t*)
            ctypes.c_uint16,  # x
            ctypes.c_uint16,  # y
            ctypes.c_uint64,  # addr
            ctypes.c_void_p,  # dst (void*)
            ctypes.c_size_t   # size
        ]
        self.lib.tt_noc_read.restype = ctypes.c_int32

        # int32_t tt_noc_write(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size);
        self.lib.tt_noc_write.argtypes = [
            ctypes.c_void_p,  # device (tt_device_t*)
            ctypes.c_uint16,  # x
            ctypes.c_uint16,  # y
            ctypes.c_uint64,  # addr
            ctypes.c_void_p,  # src (const void*)
            ctypes.c_size_t   # size
        ]
        self.lib.tt_noc_write.restype = ctypes.c_int32


    def _check_error(self, ret_code, function_name):
        if ret_code != 0:
            raise RuntimeError(f"{function_name} returned error: {ret_code}")

    def _open(self, chardev_path: str):
        # This method is called by __init__
        path_bytes = chardev_path.encode('utf-8')
        
        ret = self.lib.tt_device_open(ctypes.c_char_p(path_bytes), ctypes.byref(self._device_handle))
        self._check_error(ret, "tt_device_open")
        
        if not self._device_handle.value:
            raise RuntimeError(f"tt_device_open succeeded (returned 0) but the device handle is NULL.")

    def _close(self):
        try:
            ret = self.lib.tt_device_close(self._device_handle)
        except Exception as e:
            pass

    def __del__(self):
        self._close()

    def noc_read32(self, x: int, y: int, addr: int) -> int:
        read_value_ptr = ctypes.c_uint32()
        ret = self.lib.tt_noc_read32(
            self._device_handle, ctypes.c_uint16(x), ctypes.c_uint16(y),
            ctypes.c_uint64(addr), ctypes.byref(read_value_ptr)
        )
        self._check_error(ret, "tt_noc_read32")
        return read_value_ptr.value

    def noc_write32(self, x: int, y: int, addr: int, value: int):
        ret = self.lib.tt_noc_write32(
            self._device_handle, ctypes.c_uint16(x), ctypes.c_uint16(y),
            ctypes.c_uint64(addr), ctypes.c_uint32(value)
        )
        self._check_error(ret, "tt_noc_write32")

    def noc_read(self, x: int, y: int, addr: int, size: int) -> bytes: # Return type hint
        read_buffer = ctypes.create_string_buffer(size)
        ret = self.lib.tt_noc_read(
            self._device_handle, ctypes.c_uint16(x), ctypes.c_uint16(y),
            ctypes.c_uint64(addr), read_buffer, ctypes.c_size_t(size)
        )
        self._check_error(ret, "tt_noc_read")
        return read_buffer.raw

    def noc_write(self, x: int, y: int, addr: int, data: bytes):
        if not isinstance(data, bytes):
            raise TypeError(f"Data argument for noc_write must be bytes, not {type(data)}")

        ret = self.lib.tt_noc_write(
            self._device_handle, ctypes.c_uint16(x), ctypes.c_uint16(y),
            ctypes.c_uint64(addr),
            data,  # Pass bytes object directly (corrected from previous answer)
            ctypes.c_size_t(len(data))
        )
        self._check_error(ret, "tt_noc_write")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._close()


if __name__ == "__main__":
    device = Device(device_path="/dev/tenstorrent/0")
    x, y = 2, 11
    address = 0xffb20148
    value = device.noc_read32(x, y, address)
    print(f"Value read: 0x{value:08X}")
