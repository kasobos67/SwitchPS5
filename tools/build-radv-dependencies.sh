#!/usr/bin/env bash
# Build isolated, pinned RADV dependencies; never deploy or replace the old SDK.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
refs=$(dirname "$root")
export PS5_MESA_FORK="$refs/mihawk-mesa-review"
export PS5_PAYLOAD_SDK_FORK="$refs/mihawk-sdk-review"
vulkan="$refs/mihawk-vulkan-review"
[[ $(git -C "$vulkan" rev-parse HEAD) == 71026e7ec1951fe72ae5b9118ff0905216a4c220 ]]
[[ $(git -C "$PS5_MESA_FORK" rev-parse HEAD) == cedb774b27d089fa81f46add28d0a8c13ff0f7d2 ]]
[[ $(git -C "$PS5_PAYLOAD_SDK_FORK" rev-parse HEAD) == 95c08f27386fc698f6bbe21dde3030140a41d10b ]]
export BUILD_JOBS=24 CMAKE_BUILD_PARALLEL_LEVEL=24
mkdir -p "$root/build/radv-tools"
# Upstream calls ninja directly; enforce the same bounded parallelism everywhere.
printf '#!/bin/sh\nexec /usr/bin/ninja -j24 "$@"\n' > "$root/build/radv-tools/ninja"
chmod +x "$root/build/radv-tools/ninja"
export NINJA="$root/build/radv-tools/ninja"
command -v ccache >/dev/null
# Cross compilers do not get Meson's automatic native ccache detection.
# Keep this two-line build-only adaptation reproducible on a fresh checkout.
python3 - "$vulkan/tooling/radv/ps5-cross.ini" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
for lang, compiler in [('c', 'prospero-clang'), ('cpp', 'prospero-clang++')]:
    plain = f"{lang} = sdk / 'bin/{compiler}'"
    cached = f"{lang} = ['ccache', sdk / 'bin/{compiler}']"
    assert plain in s or cached in s, 'Unexpected upstream cross compiler configuration'
    s = s.replace(plain, cached)
p.write_text(s)
PY
bash "$vulkan/tools/setup-native-dependencies.sh"
bash "$vulkan/tools/build-radv.sh" release
python3 "$root/tools/patch-radv-wsi.py"
bash "$vulkan/tools/build-radv.sh" release
sha256sum "$vulkan/.deps/work/radv-src/src/vulkan/wsi/wsi_common_videoout.c" \
    > "$vulkan/.deps/native/radv-release/EDEN_WSI_SHA256"
