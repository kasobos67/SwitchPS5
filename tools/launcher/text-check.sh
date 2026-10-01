#!/usr/bin/env bash
# ProsperoEden - Check the launcher's text direction code against ICU (needs libicu-dev).
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/launcher/text-check.sh
# Compares pe::gfx::bidi with ICU on generated lines and on every translation of the catalogs.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build=${PROSPEROEDEN_LAUNCHER_BUILD:-"$HOME/.cache/prosperoeden-launcher"}
mkdir -p "$build/text-check"
"${HOST_CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -I"$root/headless/prosperoeden" \
    "$root/tools/launcher/text_check.cpp" "$root/headless/prosperoeden/pe/gfx/bidi.cpp" \
    -licuuc -o "$build/text-check/text_check"
# The translations alone, one per line.
lines="$build/text-check/translations.txt"
python3 - "$root/headless/prosperoeden/ui/lang" > "$lines" <<'PY'
import pathlib, re, sys
for path in sorted(pathlib.Path(sys.argv[1]).glob('*.po')):
    for match in re.finditer(r'^msgstr "((?:[^"\\]|\\.)*)"', path.read_text(encoding='utf-8'), re.M):
        text = re.sub(r'\\(.)', lambda m: {'n': '\n'}.get(m.group(1), m.group(1)), match.group(1))
        for line in text.split('\n'):
            if line:
                print(line)
PY
"$build/text-check/text_check" "$lines"
