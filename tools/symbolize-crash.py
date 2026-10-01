#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Names the functions in a ProsperoEden crash report (headless/crash_report.h).

  symbolize-crash.py REPORT ELF

REPORT is logs/crash-YYYYMMDD-HHMMSS.txt from a console. ELF is the unstripped executable of the
build that wrote it: build/headless-native/llvm-pie.elf right after a build, and for a release
the copy `make release` keeps (build/symbols/ProsperoEden-vX.Y.Z.elf). Every "eboot+0x..." in the
report is an offset into that file's code; the report is printed again with the function, file
and line after each one.

The addresses under "calls found on the stack" are where calls return to, so the function named
is the caller. The list comes from scanning the stack: the console cannot read its own code, so
it lists every value that points into the code with a "?". Those are checked here against the
ELF, and the ones that do not follow a call instruction are left out. A function that returned
earlier can still show up in the list.
"""
import re
import shutil
import struct
import subprocess
import sys


def sections(data):
    """name -> (address, file offset, size) of an ELF64 file's sections."""
    assert data[:4] == b'\x7fELF' and data[4] == 2, 'not a 64-bit ELF file'
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3A)
    headers = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    names_at = headers[shstrndx][4]
    found = {}
    for name, _kind, _flags, address, offset, size, *_ in headers:
        end = data.index(b'\0', names_at + name)
        found[data[names_at + name:end].decode()] = (address, offset, size)
    return found


def after_call(code):
    """Whether the eight bytes that end at an address end with a call (as crash_report.cpp checks)."""
    if len(code) != 8:
        return False
    if code[3] == 0xE8:  # call rel32
        return True
    for length in (2, 3, 4, 6, 7):  # a call through a register or memory: FF /2
        at = code[8 - length:]
        if at[0] == 0xFF and at[1] & 0x38 == 0x10:
            return True
        if length >= 3 and at[0] & 0xF0 == 0x40 and at[1] == 0xFF and at[2] & 0x38 == 0x10:
            return True
    return False


def tool(*names):
    for name in names:
        if shutil.which(name):
            return name
    sys.exit('not found: ' + ' or '.join(names))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    report = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    elf = sys.argv[2]
    with open(elf, 'rb') as file:
        data = file.read()
    start, at, size = sections(data)['.text']
    said = re.search(r'\(code size 0x([0-9a-f]+)\)', report)
    if not said:
        sys.exit('not a ProsperoEden crash report: ' + sys.argv[1])
    if int(said.group(1), 16) != size:
        print(f'WARNING: the report is from a build with 0x{said.group(1)} bytes of code; this ELF has 0x{size:x}. '
              'The names below are wrong unless it is the same build.\n')
    lines = report.splitlines()
    in_calls = False
    dropped = 0
    kept = []      # (line, addresses to name)
    for line in lines:
        offsets = [int(value, 16) for value in re.findall(r'eboot\+0x([0-9a-f]+)', line)]
        call = in_calls and line.startswith('  ') and len(offsets) == 1
        if line.startswith('calls found on the stack'):
            in_calls = True
        if call and line.endswith(' ?'):
            if not (8 <= offsets[0] <= size and after_call(data[at + offsets[0] - 8:at + offsets[0]])):
                dropped += 1
                continue
            line = line[:-2]
        # A return address names the instruction after the call: one byte back is inside the call.
        kept.append((line, [start + offset - (1 if call else 0) for offset in offsets]))
    wanted = [address for _line, addresses in kept for address in addresses]
    names = []
    if wanted:
        symbolizer = tool('llvm-symbolizer-18', 'llvm-symbolizer')
        output = subprocess.run([symbolizer, '-e', elf, '-f', '-C', '-i', '-p', *[hex(a) for a in wanted]],
                                capture_output=True, text=True, check=True).stdout
        names = [block.strip().replace('\n', ' ') for block in output.strip().split('\n\n')]
        if len(names) != len(wanted):  # no blank lines between answers: one answer per line
            names = [line.strip() for line in output.splitlines() if line.strip() and not line.startswith(' ')]
        assert len(names) == len(wanted), 'unexpected answer from ' + symbolizer
    for line, addresses in kept:
        found = [names.pop(0) for _address in addresses]
        print(line + ('   ' + ' | '.join(found) if found else ''))
    if dropped:
        print(f'\n{dropped} stack values that point into the code but do not follow a call were left out.')


if __name__ == '__main__':
    main()
