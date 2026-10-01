#!/usr/bin/env bash
# ProsperoEden - Draw the launcher's screens on a PC (Mesa surfaceless EGL) to PNG files.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/launcher/preview.sh [output dir] [width height]
#        tools/launcher/preview.sh --tour <video.mp4> [width height]   (needs ffmpeg)
# The same screens, shaders and font as on the console, with sample data.
# PE_LANG=<code> shows them in that language (ui/lang/<code>.po).

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source_dir="$root/headless/prosperoeden"
build=${PROSPEROEDEN_LAUNCHER_BUILD:-"$HOME/.cache/prosperoeden-launcher"}
cxx=${HOST_CXX:-c++}
mkdir -p "$build/obj"

sources=("$source_dir"/host/*.cpp "$source_dir"/pe/core/*.cpp "$source_dir"/pe/gfx/*.cpp "$source_dir"/pe/ui/*.cpp)
objects=()
pids=()
for source in "${sources[@]}"; do
    relative=${source#"$source_dir/"}
    object="$build/obj/${relative//\//_}.o"
    objects+=("$object")
    # Rebuild when the source or any launcher header is newer than the object.
    if [[ ! -e $object || $source -nt $object ]] ||
        [[ -n $(find "$source_dir/pe" "$source_dir/host" -name '*.hpp' -newer "$object" -print -quit) ]]; then
        "$cxx" -std=c++20 -O2 -Wall -Wextra -DGL_GLEXT_PROTOTYPES=1 -I"$source_dir" \
            -I"$source_dir/host" -I"$root/tools/launcher/stb" -c "$source" -o "$object" &
        pids+=($!)
    fi
done
for pid in "${pids[@]}"; do wait "$pid"; done
"$cxx" "${objects[@]}" -lEGL -lGL -lm -o "$build/pe_preview"

run() {
    EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe "$build/pe_preview" "$@"
}
if [[ ${1:-} == --tour ]]; then
    video=${2:?video file}
    width=${3:-1280}
    height=${4:-720}
    mkdir -p "$build/tour"
    run "$source_dir/ui" "$build/tour" --tour "$width" "$height" |
        ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s "${width}x${height}" -r 60 -i - \
            -c:v libx264 -pix_fmt yuv420p -crf 20 -movflags +faststart "$video"
    echo "$video"
else
    output=${1:-"$root/build/launcher-preview"}
    mkdir -p "$output"
    run "$source_dir/ui" "$output" "${@:2}"
fi
