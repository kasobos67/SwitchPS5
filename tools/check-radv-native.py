#!/usr/bin/env python3
"""Verify the combined native link's selected driver and platform ownership."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cache = Path((root/'.local/headless-cache').read_text().strip())
build = cache/'native-local'
config = (build/'CMakeCache.txt').read_text()
for option in ('EDEN_PS5_OPENGL:BOOL=ON', 'EDEN_PS5_VULKAN:BOOL=ON',
               'EDEN_VULKAN_DRIVER:STRING=RADV'):
    assert option in config, option
symbols = {}
for line in subprocess.check_output(['nm', str(build/'bin/eden-headless')], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = (parts[0], parts[1])
for name in ('__wrap_malloc', '__wrap_free', '__wrap_pthread_create', '__wrap_pthread_join',
             'ps5_fp_ieee', 'ps5___cxa_thread_atexit_impl', 'eglGetProcAddress',
             'radv_GetInstanceProcAddr', 'vkGetInstanceProcAddr'):
    assert symbols[name][1] == 't', (name, symbols.get(name))
assert symbols['vkGetInstanceProcAddr'] == symbols['radv_GetInstanceProcAddr']
assert 'ps5vk_private_open_memstream' not in symbols
# The SDK's static libc defines these as direct syscalls, which native titles may not make;
# the libkernel imports (fcntl(F_DUPFD), pipe) must be used instead.
for name in ('dup', 'pipe2'):
    assert name not in symbols, f'{name} links the SDK libc direct syscall'
link = (build/'bin/eden-headless.map').read_text()
assert 'heap_wrap' not in link, 'A second SDK heap wrapper owner was linked'
assert 'libps5vk.a' not in link and 'libpsbc.a' not in link, 'Old Vulkan implementation linked'
assert 'libvulkan_radeon.ps5.a' in link and 'libps5_opengl_core33.a' in link
sdk = root.parent/'mihawk-vulkan-review/.deps/native/ps5-payload-sdk'
for name in ('libc++.a', 'libc++abi.a', 'libunwind.a'):
    subprocess.run(['cmp', str(cache/'sdk/target/lib'/name), str(sdk/'target/lib'/name)], check=True)
crt = (build/'headless/radv_app_crt.cpp').read_text()
assert crt.index('ps5_fp_ieee();', crt.index('void _init()')) < crt.index('__preinit_array_start', crt.index('void _init()'))
print('RADV/OpenGL native link: driver selection, entrypoints, heap ownership, C++/unwind ABI and IEEE startup PASS')
