#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs the release title folder into a compressed PFS image (.ffpfsc) that ShadowMountPlus
# mounts and installs like a package: copy the image to /data/homebrew (or /data/etaHEN/games,
# or the homebrew folder of a USB or extended-storage drive) instead of the PPSA99008 folder.
# The image holds the same files as the folder, with sce_sys/param.json at its root. MkPFS
# (PSBrew/MkPFS, pinned below) is fetched into a user cache on first use; nothing of it is
# redistributed. Same procedure as ps5-native-app-boilerplate's `make ffpfsc`.
#   tools/ci/package-image.sh TITLE_FOLDER OUTPUT.ffpfsc
set -euo pipefail
[[ $# -eq 2 ]] || { echo "usage: $0 TITLE_FOLDER OUTPUT.ffpfsc" >&2; exit 2; }
folder=$(cd -- "$1" && pwd)
output=$2
[[ -f $folder/sce_sys/param.json && -f $folder/eboot.bin ]] || {
    echo "$folder is not a title folder (sce_sys/param.json and eboot.bin)" >&2
    exit 2
}
for command in git python3; do
    command -v "$command" >/dev/null || { echo "missing required command: $command" >&2; exit 2; }
done
revision=6cb8313dfe0c988ac52617794553f343243d3a56
checkout="${XDG_CACHE_HOME:-$HOME/.cache}/prosperoeden-mkpfs/$revision"
if [[ ! -d $checkout/.git ]]; then
    rm -rf -- "$checkout"
    mkdir -p "$checkout"
    git -C "$checkout" init --quiet
    git -C "$checkout" remote add origin https://github.com/PSBrew/MkPFS.git
    git -C "$checkout" fetch --quiet --depth 1 origin "$revision"
    git -C "$checkout" checkout --quiet --detach FETCH_HEAD
fi
[[ $(git -C "$checkout" rev-parse HEAD) == "$revision" ]] && git -C "$checkout" diff --quiet || {
    echo "MkPFS cache $checkout is not a clean checkout of $revision" >&2
    exit 2
}
python="$checkout/.venv/bin/python"
if [[ ! -x $python ]]; then
    python3 -m venv "$checkout/.venv" || { echo "python3 venv support is required (python3-venv)" >&2; exit 2; }
fi
if [[ ! -f $checkout/.venv/.installed ]]; then
    "$python" -m pip install --disable-pip-version-check --quiet "$checkout" >&2
    touch "$checkout/.venv/.installed"
fi
rm -f -- "$output"
log="$output.log"
# Wrapped-folder mode (MkPFS's maximum-compatibility .ffpfsc layout), verified after writing.
if ! "$python" -m mkpfs pack folder --no-adjust-output-file-extension --version PS5 --verify \
        "$folder" "$output" > "$log" 2>&1; then
    tail -40 "$log" >&2
    exit 1
fi
grep -q '^Errors: *0$' "$log" || { tail -40 "$log" >&2; echo "MkPFS verification reported errors" >&2; exit 1; }
rm -f -- "$log"
[[ -s $output ]] || { echo "MkPFS did not write $output" >&2; exit 1; }
