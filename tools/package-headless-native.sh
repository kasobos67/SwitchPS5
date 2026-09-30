#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
[[ $# == 0 || ( $# == 1 && ( "$1" == --integration || "$1" == --game ) ) ]]
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(cat "$root/.local/headless-cache")
[[ "$(cat "$scratch/owner")" == "$root" ]]
template="$root/../ps5-native-app-boilerplate"
builder="$root/build/host/ps5-native-tool"
out="$root/build/headless-native"
app="${EDEN_PACKAGE_DIR:-$root/dist/headless/PPSA99008}"
# A separate clean staging directory lets driver candidates omit private game
# assets without deleting or copying the existing development installation.
if [[ -n ${EDEN_PACKAGE_DIR:-} ]]; then
    [[ $(realpath -m "$app") == "$root"/build/*/PPSA99008 ]] || exit 2
    [[ ${1:-} != --game ]] || exit 2
fi
test -x "$builder"
(cd "$template/runtime" && sha256sum --check --strict libc.prx.sha256)
mkdir -p "$out" "$app/sce_sys" "$app/sce_module"
cp "$scratch/native-local/bin/eden-headless" "$out/llvm-pie.elf"
cp "$scratch/native-local/bin/eden-headless.map" "$out/link.map"
cp "$scratch/native-local/CMakeCache.txt" "$out/CMakeCache.txt"
stub_flags=(--stub-dir "$scratch/sdk/target/lib")
if grep -qx 'EDEN_PS5_OPENGL:BOOL=ON' "$out/CMakeCache.txt"; then
    gl46="$root/.deps/ps5-opengl-sdk-0.6.0/sdk"
    stub_flags=()
    for stub in "$scratch/sdk/target/lib/"*.so; do
        case "${stub##*/}" in libSceAgc.so|libSceAgcDriver.so) continue ;; esac
        stub_flags+=(--stub "$stub")
    done
    driver_stub="$gl46/lib/libSceAgcDriver.so"
    if grep -qx 'EDEN_PS5_VULKAN:BOOL=ON' "$out/CMakeCache.txt"; then
        driver_stub="$root/build/stubs/libSceAgcDriver.so"
    fi
    stub_flags+=(--stub "$gl46/lib/libSceAgc.so" --stub "$driver_stub")
fi
"$builder" link --in "$out/llvm-pie.elf" --out "$out/eboot.elf" \
    "${stub_flags[@]}" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
"$builder" self --sign --in "$out/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$template/runtime/libc.prx" "$app/sce_module/libc.prx"
cp "$root/assets/"{icon0.png,pic0.dds,pic1.dds,snd0.at9} "$app/sce_sys/"
rm -rf "$app/ui"
cp -a "$root/headless/prosperoeden/ui" "$app/ui"
# Filesystem access helper (headless/elevation, built for PPSA99008): elfldr runs it at startup.
make -s -C "$root/headless/elevation/helper" OUTPUT="$root/build/elevation/sandbox-elevator.elf" \
    PS5_PAYLOAD_SDK="${PS5_ELEVATION_SDK:-/opt/ps5-payload-sdk}"
python3 "$root/headless/elevation/validate-helper.py" "$root/build/elevation/sandbox-elevator.elf"
cp "$root/build/elevation/sandbox-elevator.elf" "$app/sandbox-elevator.elf"
python3 - "$root" "$template" "$app" "${1:-}" <<'PY'
import json, pathlib, runpy, shutil, sys
root, template, app = map(pathlib.Path, sys.argv[1:4])
game = sys.argv[4] == '--game'
integration = sys.argv[4] in ('--integration', '--game')
if game:
    assert (app / 'game-assets.json').is_file()
    assert (app / 'assets/keys/prod.keys').is_file()
value = json.loads((template / 'sce_sys/param.json').read_text())
value.update(titleId='PPSA99008', conceptId='99008', contentId='UP9000-PPSA99008_00-PROSPEROEDEN0001')
value['contentVersion'] = '01.000.030'
value['localizedParameters']['en-US']['titleName'] = 'ProsperoEden'
value['pubtools']['loudnessSnd0'] = '-28.00'
(app / 'sce_sys/param.json').write_text(json.dumps(value, indent=2) + '\n')
runpy.run_path(str(root / 'tools/load_alignment.py'))['check_load_alignment']((app / 'eboot.bin').read_bytes())
profile = (root / 'build/headless-native/CMakeCache.txt').read_text()
assert sum(profile.count('EDEN_DEVICE_FRONTEND:BOOL=' + v) for v in ('ON', 'OFF')) == 1
devices = 'EDEN_DEVICE_FRONTEND:BOOL=ON' in profile
graphics = 'EDEN_PS5_OPENGL:BOOL=ON' in profile
assert not (integration and devices)
assert not game or graphics
fixture_name = 'core-integration.nro' if integration else 'core-devices.nro' if devices else 'core-homebrew.nro'
shutil.copy2(root / 'build/fixture' / fixture_name, app / 'core-homebrew.nro')
(root / 'build/headless-native/frontend.json').write_text(json.dumps({
    'devices': devices,
    'gpu_probe': 'EDEN_GPU_PROBE:BOOL=ON' in profile,
    'renderer': 'opengl-4.6-compatibility' if graphics else 'null', 'guest_fixture': 'retail-game' if game else 'cpu-state-memory-atomics-fp-simd-services' if integration else 'timer-sync-service-storage-hid-audio-renderer-voice-mix-src-high' if devices else 'timer-sync-service-storage',
    'vulkan': 'EDEN_PS5_VULKAN:BOOL=ON' in profile,
    'vulkan_driver': 'RADV' if 'EDEN_VULKAN_DRIVER:STRING=RADV' in profile else 'CUSTOM',
    'development_backend': 'Vulkan' if 'EDEN_DEV_VULKAN:BOOL=ON' in profile else 'user-preference',
    'development_rom_id': next((line.split('=', 1)[1] for line in profile.splitlines()
                                if line.startswith('EDEN_DEV_ROM_ID:STRING=')), ''),
    'hardware_qualified': False,
}, indent=2) + '\n')
PY
llvm-readobj-18 --dyn-symbols --needed-libs "$out/llvm-pie.elf" > "$out/imports.txt"
"$builder" self --inspect --file "$app/eboot.bin" > "$out/fself-inspection.txt"
sha256sum "$app/eboot.bin" "$app/core-homebrew.nro" "$app/sce_module/libc.prx" \
    "$app/sce_sys/param.json" "$app/sce_sys/icon0.png" "$app/sce_sys/pic0.dds" "$app/sce_sys/pic1.dds" \
    "$app/sce_sys/snd0.at9"
