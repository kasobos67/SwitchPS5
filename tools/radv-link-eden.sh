#!/usr/bin/env bash
# Source after setting root to the Eden port. Keep the qualified Eden heap;
# reuse the pinned RADV platform recipe for threads, libc, TLS and unwinding.
eden_radv_link_recipe() {
    local archive=$1
    local reference="$root/../mihawk-vulkan-review"
    local radv_sdk="$reference/.deps/native/ps5-payload-sdk"
    source "$reference/tools/radv-link.sh"
    radv_link_recipe "$reference" "$radv_sdk" "$archive" || return
    # The Eden linker already groups all native archives. lld rejects nested
    # groups, so flatten only the recipe's group delimiters.
    local input
    local inputs=()
    for input in "${radv_link_inputs[@]}"; do
        case "$input" in --start-group|--end-group) ;; *) inputs+=("$input") ;; esac
    done
    radv_link_inputs=("${inputs[@]}")
    local flag
    local retained=()
    for flag in "${radv_link_flags[@]}"; do
        case "$flag" in
            --wrap=malloc|--wrap=calloc|--wrap=realloc|--wrap=free|\
            --wrap=posix_memalign|--wrap=aligned_alloc|--wrap=memalign|\
            --wrap=malloc_usable_size|--wrap=reallocf|--wrap=reallocarray|\
            --wrap=getline|--wrap=getdelim) ;;
            *) retained+=("$flag") ;;
        esac
    done
    # The static Mesa ICD exports radv_GetInstanceProcAddr. Eden's existing
    # static loader uses the standard public name.
    radv_link_flags=("${retained[@]}" --defsym=vkGetInstanceProcAddr=radv_GetInstanceProcAddr
        --undefined=__real_fclose --undefined=__real_fflush)
    # Mesa generates weak optional dispatch entries. A native title has no ELF
    # loader to resolve absent entries to NULL; keep that static-link behavior
    # explicitly, without changing real implementations or SDK imports.
    local optional_script="$root/build/radv-isolated/optional-entrypoints.ld"
    python3 - "$archive" "$reference/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a" "$optional_script" <<'PY'
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

archive, original, output = map(Path, sys.argv[1:])

def bindings(path):
    result = subprocess.run(['nm', '-g', '--format=posix', str(path)],
                            check=True, capture_output=True, text=True)
    found = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and len(parts[1]) == 1:
            found.setdefault(parts[0], set()).add(parts[1])
    return found

current = bindings(archive)
upstream = bindings(original)
prefixes = ('radv_', 'wsi_', 'vk_common_', 'annotate_', 'ctx_roll_',
            'rmv_', 'rra_', 'utrace_', 'sqtt_', 'threaded_')
optional = sorted(name for name, kinds in current.items()
                  if kinds <= {'w', 'v'} and name.startswith(prefixes))
assert optional, 'No optional RADV dispatch entries found'
for name in optional:
    assert re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name), name
    assert upstream.get(name, set()) and upstream[name] <= {'w', 'v'}, name
output.write_text('/* Absent optional Mesa entrypoints; real definitions take precedence. */\n'
                  + ''.join(f'PROVIDE_HIDDEN({name} = 0);\n' for name in optional))
output.with_suffix('.json').write_text(json.dumps({
    'archive_sha256': hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest(),
    'original_sha256': hashlib.file_digest(original.open('rb'), 'sha256').hexdigest(),
    'optional_entrypoint_count': len(optional), 'optional_entrypoints': optional,
    'link_script_sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
    'rule': 'Only absent weak dispatch references in both current and original RADV archives; PROVIDE_HIDDEN preserves real definitions.'
}, indent=2) + '\n')
print(f'RADV optional dispatch entries: {len(optional)} static NULL bindings')
PY
    radv_link_flags+=(-T "$optional_script")
}
