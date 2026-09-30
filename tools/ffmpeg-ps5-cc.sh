#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=${PS5_PAYLOAD_SDK:?}
arguments=()
link=true
for argument in "$@"; do
    case "$argument" in
        -c|-E|-S|-M|-MM|--version) link=false; arguments+=("$argument") ;;
        -lm|-lpthread|-pthread) ;;
        *) arguments+=("$argument") ;;
    esac
done
if "$link"; then
    # Configure really links against the native providers, but never executes
    # its cross-target probes. These probe ELFs are not application packages.
    export PATH="$root/../ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk/bin:$PATH"
    arguments+=(-nostdlib
        -Wl,-e,main -Wl,--no-undefined -L "$sdk/target/lib"
        -lSceLibcInternal -lkernel -lc -lSceNet)
fi
exec clang-18 --target=x86_64-sie-ps5 -isysroot "$sdk" -isystem "$sdk/target/include" \
    -fvisibility-nodllstorageclass=default -fno-stack-protector -fno-plt -femulated-tls \
    -ffunction-sections -fdata-sections "${arguments[@]}"
