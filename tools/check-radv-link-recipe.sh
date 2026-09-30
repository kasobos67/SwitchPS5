#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$root/tools/radv-link-eden.sh"
# This checks recipe composition, not a native link. An empty regular file
# satisfies the recipe's path check without requiring a dependency rebuild.
archive=$(mktemp)
trap 'rm -f -- "$archive"' EXIT
eden_radv_link_recipe "$archive"
flags=$(printf '%s\n' "${radv_link_flags[@]}")
for required in --wrap=pthread_create --wrap=pthread_join --wrap=pthread_detach \
    --wrap=fflush --wrap=fclose --undefined=__real_fclose --undefined=__real_fflush \
    --defsym=__cxa_thread_atexit_impl=ps5___cxa_thread_atexit_impl \
    --defsym=vkGetInstanceProcAddr=radv_GetInstanceProcAddr; do
    grep -Fxq -- "$required" <<< "$flags"
done
if grep -Eq '^--wrap=(malloc|calloc|realloc|free|posix_memalign|aligned_alloc|memalign|malloc_usable_size|reallocf|reallocarray|getline|getdelim)$' <<< "$flags"; then
    echo 'RADV recipe must not pull a second heap-wrapper owner' >&2
    exit 1
fi
printf '%s\n' "${radv_link_inputs[@]}" | grep -Fxq -- --whole-archive
if printf '%s\n' "${radv_link_inputs[@]}" | grep -Eq '^--(start|end)-group$'; then
    echo 'RADV inputs must reuse the outer Eden archive group' >&2
    exit 1
fi
echo 'RADV link recipe: Eden heap ownership, thread/libc/TLS flags and static entrypoint PASS'
