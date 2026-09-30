# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that the mapped ELF segments of a signed native container are 16 KiB aligned."""
import struct


def check_load_alignment(data):
    """Check mapped ELF segments inside our native container, without unpacking."""
    if len(data) < 32 or data[:4] != bytes.fromhex("4f153d1d"):
        raise ValueError("Expected native container header")
    elf = 32 + struct.unpack_from("<H", data, 24)[0] * 32
    if elf + 64 > len(data) or data[elf:elf + 6] != b"\x7fELF\x02\x01":
        raise ValueError("Missing ELF64 little-endian header")
    table = elf + struct.unpack_from("<Q", data, elf + 32)[0]
    stride, count = struct.unpack_from("<HH", data, elf + 54)
    if stride != 56 or not count or table < elf + 64 or table + stride * count > len(data):
        raise ValueError("Invalid ELF program header table")
    loads = []
    for position in range(table, table + stride * count, stride):
        kind, flags, offset, address, _, size, memory, alignment = struct.unpack_from("<II6Q", data, position)
        if kind == 1 and flags:
            if alignment != 16384 or offset % 16384 or address % 16384 or size > memory:
                raise ValueError(f"Mapped segment is not 16 KiB aligned: offset={offset:#x}, address={address:#x}")
            loads.append(position)
    if not loads:
        raise ValueError("No mapped ELF segments")
    return loads
