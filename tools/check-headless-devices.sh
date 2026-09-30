#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(cat "$root/.local/headless-cache")
[[ "$(cat "$scratch/owner")" == "$root" ]]
run=$(mktemp -d "$root/results/devices-host-$(date -u +%Y%m%d-%H%M%S).XXXXXX")
mkdir "$run/user"
cp "$scratch/build/bin/eden-devices-check" "$run/"
printf 'Device evidence: %s\n' "$run"
cd "$run"
sha256sum eden-devices-check > hashes.txt
timeout --kill-after=5s 15s ./eden-devices-check > result.txt 2> stderr.log
cat result.txt
