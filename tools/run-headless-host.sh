#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
limit=30
for flag in "$@"; do
    [[ "$flag" == --repeat || "$flag" == --devices || "$flag" == --soak || "$flag" == --integration ]]
    if [[ "$flag" == --soak ]]; then limit=120; fi
done
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mkdir -p results
run=$(mktemp -d "$root/results/headless-host-$(date -u +%Y%m%d-%H%M%S).XXXXXX")
mkdir "$run/user"
printf 'Headless evidence: %s\n' "$run"
cp build/headless-host/eden-headless "$run/eden-headless"
fixture=core-homebrew
checks=()
run_flags=()
for flag in "$@"; do if [[ "$flag" != --integration ]]; then run_flags+=("$flag"); fi; done
for flag in "$@"; do if [[ "$flag" == --devices ]]; then fixture=core-devices; checks+=(--guest-devices --renderer --renderer-voice --renderer-mix --renderer-src --renderer-high); fi; done
for flag in "$@"; do if [[ "$flag" == --integration ]]; then [[ "$fixture" != core-devices ]]; fixture=core-integration; fi; done
cp "build/fixture/$fixture.nro" "$run/core-homebrew.nro"
cd "$run"
sha256sum eden-headless core-homebrew.nro > hashes.txt
/usr/bin/time -v -o time.txt timeout --kill-after=5s "${limit}s" \
    ./eden-headless ./core-homebrew.nro "${run_flags[@]}" \
    > result.tsv 2> stderr.log
python3 -B "$root/headless/check.py" "$run" "$@" --metadata --services "${checks[@]}"
