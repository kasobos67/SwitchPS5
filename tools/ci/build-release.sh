#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the release ZIP (dist/ProsperoEden-vX.Y.Z.zip, SHA256SUMS, release-notes.md)
# from this checkout: Eden with the OpenGL and RADV Vulkan renderers, packaged as
# PPSA99008. EDEN_DEV_CHECKOUT names a development checkout that already holds the
# prebuilt inputs beside its sibling repositories; they are linked in, not copied.
# See docs/BUILDING.md.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
dev=$(cd -- "${EDEN_DEV_CHECKOUT:?Set EDEN_DEV_CHECKOUT; see docs/BUILDING.md}" && pwd)
if [[ $dev != "$root" ]]; then
    for sibling in ps5-native-app-boilerplate ps5-yamagi mihawk-vulkan-review ps5-vulkan-eden ps5-radio-browser; do
        source_dir=$(cd -- "$dev/../$sibling" && pwd)
        [[ -e ../$sibling ]] || ln -s "$source_dir" "../$sibling"
    done
    mkdir -p .deps build
    for input in eden-5f142c79.tar.gz compiler-rt-18.1.8 fmt-12.1.0 ps5-opengl-diagnostic ps5-opengl-sdk-local-3b66914; do
        ln -sfn "$dev/.deps/$input" ".deps/$input"
    done
    ln -sfn "$dev/build/radv-isolated" build/radv-isolated
fi

# Relative paths in ccache keys let other checkouts and caches reuse compiled objects.
export CCACHE_BASEDIR=${CCACHE_BASEDIR:-/}
# One build cache per checkout, kept between runs (the development cache is not touched).
cache_parent="${XDG_CACHE_HOME:-$HOME/.cache}"
if [[ ! -f .local/headless-cache ]]; then
    scratch="$cache_parent/ps5-eden-headless.$(printf '%s' "$root" | sha256sum | cut -c1-12)"
    mkdir -p .local "$scratch"
    printf '%s\n' "$root" > "$scratch/owner"
    printf '%s\n' "$scratch" > .local/headless-cache
fi
scratch=$(cat .local/headless-cache)
eden="$scratch/source"
if [[ ! -f $eden/CMakeLists.txt ]]; then
    python3 - <<'PY'
import hashlib, json, pathlib
p = pathlib.Path('.deps/eden-5f142c79.tar.gz')
assert hashlib.sha256(p.read_bytes()).hexdigest() == json.loads(pathlib.Path('UPSTREAM.json').read_text())['archives'][p.name]
PY
    mkdir -p "$eden"
    tar -xzf .deps/eden-5f142c79.tar.gz --strip-components=1 -C "$eden"
fi
printf '%s\n' '5f142c7926d0c7fcbbd0ce30794d72f638a43b2a' > "$eden/GIT-COMMIT"
printf '%s\n' 'ps5-headless' > "$eden/GIT-REFSPEC"
# Eden's hash-pinned CPM packages, as the development cache fetched them.
if [[ ! -f $eden/.cache/cpm/ffmpeg/c7b5f1537d/configure ]]; then
    mkdir -p "$eden/.cache"
    cp -a "$(cat "$dev/.local/headless-cache")/source/.cache/cpm" "$eden/.cache/"
fi
if [[ ! -f $scratch/sdk/.complete ]]; then
    mkdir -p "$scratch/sdk"
    cp -a ../ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk/target "$scratch/sdk/"
    touch "$scratch/sdk/.complete"
fi
[[ -f $scratch/ffmpeg-native/install/lib/libavcodec.a ]] || bash tools/build-headless-ffmpeg.sh
bash tools/build-native-tool.sh

EDEN_PS5_VULKAN=ON EDEN_VULKAN_DRIVER=RADV EDEN_DEV_VULKAN=OFF EDEN_DEV_ROM_ID= \
    EDEN_DEV_PROFILE=OFF EDEN_DEV_WAIT_CALLERS=OFF bash tools/build-headless-native.sh --graphics
python3 -B tools/check-radv-native.py
export EDEN_PACKAGE_DIR="$root/build/release/PPSA99008"
rm -rf "$EDEN_PACKAGE_DIR"
PS5_ELEVATION_SDK="$root/../ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk" \
    bash tools/package-headless-native.sh --integration
python3 -B headless/check_package.py --check

rm -rf dist
mkdir -p dist
python3 - <<'PY'
import hashlib, json, os, pathlib, re, zipfile
root = pathlib.Path('.')
app = pathlib.Path(os.environ['EDEN_PACKAGE_DIR'])
version = json.loads((app / 'sce_sys/param.json').read_text())['contentVersion']
tag = 'v' + version.removeprefix('0')
archive = root / f'dist/ProsperoEden-{tag}.zip'
files = [(p, 'PPSA99008/' + p.relative_to(app).as_posix()) for p in sorted(app.rglob('*')) if p.is_file()]
files += [(root / name, name) for name in ('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')]
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zip_file:
    for path, name in files:
        info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        zip_file.writestr(info, path.read_bytes())
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(root / 'dist/SHA256SUMS').write_text(f'{digest}  {archive.name}\n')
readme = (root / 'README.md').read_text()
match = re.search(rf'^## Changes in {re.escape(tag)}\n(.*?)(?=^## )', readme, re.M | re.S)
assert match, f'README.md has no "## Changes in {tag}" section'
(root / 'dist/release-notes.md').write_text(match.group(1).strip() + '\n')
print(f'{archive} {digest}')
PY
