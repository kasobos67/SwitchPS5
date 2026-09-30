#!/usr/bin/env python3
"""Keep the RADV/Mesa implementation private beside the OpenGL Mesa build.

Only defined implementation symbols are renamed, together with their references
inside the archive. Public Vulkan entrypoints and shared C++ runtime identities
remain unchanged. The input release archive is never modified.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1]
source = root.parent/'mihawk-vulkan-review/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a'
output = root/'build/radv-isolated'
output.mkdir(parents=True, exist_ok=True)

def symbols(path, defined=False):
    cmd = ['nm', '-g', '--format=posix']
    if defined:
        cmd.append('--defined-only')
    result = subprocess.run(cmd+[str(path)], check=True, capture_output=True, text=True)
    return {p[0] for line in result.stdout.splitlines()
            if len(p := line.split()) >= 2 and len(p[1]) == 1}

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

defined = sorted(symbols(source, True))
demangled = subprocess.run(['c++filt'], input='\n'.join(defined)+'\n',
                           check=True, capture_output=True, text=True).stdout.splitlines()
assert len(defined) == len(demangled)
public = {s for s in defined if re.match(r'vk[A-Z]|vk_icd', s)} | {'radv_GetInstanceProcAddr'}
assert 'radv_GetInstanceProcAddr' in defined
private = []
for name, readable in zip(defined, demangled):
    # A std:: container instantiated with aco/GLSL types still belongs to that
    # compiler version. Isolate its weak methods too, or the linker may silently
    # coalesce incompatible implementations from the OpenGL compiler archive.
    shared_cpp = ((re.match(r'^(typeinfo (for|name for) std::|vtable for std::)', readable)
                   and '<' not in readable)
                  or readable.startswith(('operator new', 'operator delete'))
                  or name.startswith(('__cxa_', '__gxx_', '_Unwind_')))
    if name not in public and not shared_cpp:
        private.append(name)
mapping = output/'symbols.map'
mapping.write_text(''.join(f'{s} eden_radv_private_{s}\n' for s in private))
before = digest(source)
target = output/source.name
subprocess.run(['objcopy', '--redefine-syms='+str(mapping), str(source), str(target)], check=True)
subprocess.run(['ranlib', str(target)], check=True)
assert digest(source) == before, 'Input RADV archive changed'
after_symbols = symbols(target)
assert not set(private).intersection(after_symbols), 'Unrenamed implementation symbol'
assert public.intersection(defined) <= symbols(target, True), 'Lost public entrypoint'
(output/'manifest.json').write_text(json.dumps({
    'source': str(source), 'source_sha256': before,
    'isolated_sha256': digest(target), 'renamed_symbols': len(private),
    'public_symbols': sorted(public.intersection(defined)),
    'note': 'Archive namespace verification only; final native link and runtime validation still required.'
}, indent=2)+'\n')
print(f'RADV isolated: {len(private)} implementation symbols; {target}')
