#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds build/stubs/libSceAgcDriver.so, the libSceAgcDriver import facade both graphics
# drivers link against (tools/stubs/libSceAgcDriver.c), with the Payload SDK's compiler.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="$root/../ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk"
source="$root/tools/stubs/libSceAgcDriver.c"
out="$root/build/stubs/libSceAgcDriver.so"
[[ -f $out && $out -nt $source ]] && exit 0
test -x "$sdk/bin/prospero-clang" || { echo "Missing the Payload SDK; run make deps" >&2; exit 1; }
mkdir -p "$root/build/stubs"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
PS5_PAYLOAD_SDK="$sdk" "$sdk/bin/prospero-clang" -fPIC -c "$source" -o "$work/libSceAgcDriver.o"
"$sdk/bin/prospero-lld" --shared -soname libSceAgcDriver.prx -o "$work/libSceAgcDriver.so" "$work/libSceAgcDriver.o"
mv "$work/libSceAgcDriver.so" "$out"
