#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds build/host/ps5-native-tool (links, signs and inspects the eboot) from the
# native app boilerplate's tooling.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
template="$root/../ps5-native-app-boilerplate"
native="$template/tooling/native"
zlib="$template/.deps/native/zlib/root"
mkdir -p "$root/build/host"
clang++-18 -std=c++20 -O2 -Wall -Wextra -Werror -I "$zlib/usr/include" \
    "$native/native_app_builder.cpp" "$native/self_container.cpp" \
    "$native/elf_object.cpp" "$native/sce_module_writer.cpp" "$zlib/usr/lib/libz.a" \
    -o "$root/build/host/ps5-native-tool"
