#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Release files from a built PPSA99008 folder (make release, make image).

  tools/ci/make-dist.py [--image-only] APP_DIR

dist/ProsperoEden-vX.Y.Z.zip (the folder plus README, LICENSE and THIRD_PARTY_NOTICES; fixed
timestamps, so equal inputs give equal bytes), dist/ProsperoEden-vX.Y.Z.ffpfsc (the same title as
a package image, tools/ci/package-image.sh), SHA256SUMS and release-notes.md (the README's
"Changes in vX.Y.Z" section).
"""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

root = pathlib.Path(__file__).resolve().parents[2]
args = sys.argv[1:]
image_only = args[:1] == ['--image-only']
if image_only:
    args = args[1:]
if len(args) != 1:
    sys.exit(__doc__.strip())
app = pathlib.Path(args[0]).resolve()
version = json.loads((app / 'sce_sys/param.json').read_text())['contentVersion']
tag = 'v' + version.removeprefix('0')
dist = root / 'dist'
image = dist / f'ProsperoEden-{tag}.ffpfsc'
if image_only:
    dist.mkdir(exist_ok=True)
    subprocess.run(['bash', str(root / 'tools/ci/package-image.sh'), str(app), str(image)], check=True)
    print(f'{image} {hashlib.sha256(image.read_bytes()).hexdigest()}')
    sys.exit(0)
shutil.rmtree(dist, ignore_errors=True)
dist.mkdir()
archive = dist / f'ProsperoEden-{tag}.zip'
files = [(p, 'PPSA99008/' + p.relative_to(app).as_posix()) for p in sorted(app.rglob('*')) if p.is_file()]
files += [(root / name, name) for name in ('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md') if (root / name).exists()]
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zip_file:
    for path, name in files:
        info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        zip_file.writestr(info, path.read_bytes())
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
subprocess.run(['bash', str(root / 'tools/ci/package-image.sh'), str(app), str(image)], check=True)
image_digest = hashlib.sha256(image.read_bytes()).hexdigest()
(dist / 'SHA256SUMS').write_text(f'{digest}  {archive.name}\n{image_digest}  {image.name}\n')
readme = (root / 'README.md').read_text()
match = re.search(rf'^## Changes in {re.escape(tag)}\n(.*?)(?=^## )', readme, re.M | re.S)
if match:
    (dist / 'release-notes.md').write_text(match.group(1).strip() + '\n')
else:
    print(f'README.md has no "## Changes in {tag}" section; no release-notes.md', file=sys.stderr)
print(f'{archive} {digest}')
print(f'{image} {image_digest}')
