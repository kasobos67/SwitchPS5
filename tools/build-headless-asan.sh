#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(cat "$root/.local/headless-cache")
[[ "$(cat "$scratch/owner")" == "$root" ]]
mkdir -p "$root/build/headless-asan"
cmake -S "$scratch/source" -B "$scratch/build" -DEDEN_HOST_ASAN=ON -DCMAKE_EXE_LINKER_FLAGS=
cmake --build "$scratch/build" --target eden-headless -j6
cp "$scratch/build/bin/eden-headless" "$root/build/headless-asan/eden-headless"
# The normal binary stays unchanged; restore ordinary cached objects with build-headless-host.sh.
