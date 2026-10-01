#!/usr/bin/env python3
"""Reject compiler-specific weak/strong overlap across all linked Mesa archives."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
def defined(path):
    lines = subprocess.check_output(['nm', '-g', '--defined-only', '--format=posix', str(path)], text=True).splitlines()
    return {p[0] for line in lines if len(p := line.split()) > 1 and len(p[1]) == 1}

gl = set()
for archive in (root/'.deps/ps5-opengl-sdk-1.0.0/sdk/lib').glob('*.a'):
    with archive.open('rb') as stream:
        if stream.read(8) == b'!<arch>\n':
            gl.update(defined(archive))
assert gl, 'OpenGL archive inventory missing'
shared = sorted(defined(root/'build/radv-isolated/libvulkan_radeon.ps5.a') & gl)
names = subprocess.run(['c++filt'], input='\n'.join(shared)+'\n', capture_output=True,
                       text=True, check=True).stdout.splitlines() if shared else []
unsafe = []
for name in names:
    runtime_identity = name.startswith(('typeinfo for std::', 'typeinfo name for std::', 'vtable for std::')) and '<' not in name
    allocation_operator = name.startswith(('operator new', 'operator delete'))
    if not runtime_identity and not allocation_operator:
        unsafe.append(name)
assert not unsafe, f'Compiler symbol collision ({len(unsafe)}): {unsafe[:3]}'
print(f'RADV vs every OpenGL compiler archive: no implementation overlap; {len(shared)} shared standard runtime identities/operators PASS')
