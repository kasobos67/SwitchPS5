#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
limit=30
for flag in "$@"; do
    [[ "$flag" == --repeat || "$flag" == --devices || "$flag" == --soak ]]
    if [[ "$flag" == --soak ]]; then limit=120; fi
done
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(cat "$root/.local/headless-cache")
[[ "$(cat "$scratch/owner")" == "$root" ]]
# The ordinary host builder explicitly clears this audit-only linker flag.
cmake -S "$scratch/source" -B "$scratch/build" -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=leak -DEDEN_HOST_ASAN=OFF \
    -DSDL_X11=OFF -DSDL_WAYLAND=OFF > "$root/.deps/headless-lsan-build.log" 2>&1
cmake --build "$scratch/build" --target eden-headless -j6 >> "$root/.deps/headless-lsan-build.log" 2>&1
run=$(mktemp -d "$root/results/headless-lsan-$(date -u +%Y%m%d-%H%M%S).XXXXXX")
mkdir "$run/user"
cp "$scratch/build/bin/eden-headless" "$run/eden-headless"
fixture=core-homebrew
checks=()
for flag in "$@"; do if [[ "$flag" == --devices ]]; then fixture=core-devices; checks+=(--guest-devices --renderer --renderer-voice --renderer-mix --renderer-src --renderer-high); fi; done
cp "$root/build/fixture/$fixture.nro" "$run/core-homebrew.nro"
printf 'Memory evidence: %s\n' "$run"
cd "$run"
set +e
LSAN_OPTIONS=verbosity=1:log_threads=1:exitcode=23:external_symbolizer_path=/usr/bin/llvm-symbolizer-18 \
    timeout --kill-after=5s "${limit}s" ./eden-headless ./core-homebrew.nro "$@" > result.tsv 2> stderr.log
status=$?
set -e
printf '%s\n' "$status" > exit-status.txt
sha256sum eden-headless core-homebrew.nro result.tsv stderr.log > hashes.txt
python3 -B "$root/headless/check.py" "$run" "$@" --metadata --services "${checks[@]}"
exit "$status"
