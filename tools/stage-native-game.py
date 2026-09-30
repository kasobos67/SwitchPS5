#!/usr/bin/env python3
"""Stage owner-supplied assets in the ignored native package; never print key contents."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('game', type=Path)
parser.add_argument('keys', type=Path)
parser.add_argument('firmware', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
app = root / 'dist/headless/PPSA99008'
assert args.game.is_file() and args.game.suffix.lower() in ('.nsp', '.xci')
sources = {
    f'assets/roms/{args.game.name}': args.game,
    'assets/keys/prod.keys': args.keys / 'prod.keys',
    'assets/roms/README.txt': root / 'headless/roms-readme.txt',
}
if (args.keys / 'title.keys').is_file():
    sources['assets/keys/title.keys'] = args.keys / 'title.keys'
firmware = sorted(args.firmware.glob('*.nca'))
assert firmware and all(re.fullmatch(r'[0-9a-fA-F]{32}(?:\.cnmt)?\.nca', p.name) for p in firmware)
sources.update({f'assets/firmware/{p.name}': p for p in firmware})
assert all(p.is_file() and not p.is_symlink() for p in sources.values())
manifest = {}
for name, source in sources.items():
    dest = app / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)
    with dest.open('rb') as stream:
        manifest[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
(app / 'game-assets.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Staged {len(manifest)} private game/firmware/key files; originals unchanged')
