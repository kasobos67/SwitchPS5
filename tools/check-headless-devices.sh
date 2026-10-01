#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(cat "$root/.local/headless-cache")
[[ "$(cat "$scratch/owner")" == "$root" ]]
# A fresh checkout has no results folder yet. The check runs from the build cache: the run's
# folder keeps what it wrote and the executable's hash, not a copy of the executable.
mkdir -p "$root/results"
run=$(mktemp -d "$root/results/devices-host-$(date -u +%Y%m%d-%H%M%S).XXXXXX")
mkdir "$run/user"
printf 'Device evidence: %s\n' "$run"
cd "$run"
(cd "$scratch/build/bin" && sha256sum eden-devices-check) > hashes.txt
timeout --kill-after=5s 15s "$scratch/build/bin/eden-devices-check" > result.txt 2> stderr.log
cat result.txt
