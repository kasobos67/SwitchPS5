#!/usr/bin/env python3
"""Guest save persistence across process restarts; commit durability is not claimed."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'headless'))
from check import receipt, game_log_is_clean

case = Path(tempfile.mkdtemp(prefix='guest-save-', dir=root / 'results'))
print(case, flush=True)
binary = case / 'eden-headless'
shutil.copy2(root / 'build/headless-host/eden-headless', binary)
for name in ('write', 'read'):
    shutil.copy2(root / f'build/fixture/core-save-{name}.nro', case / f'{name}.nro')
data = case / 'data'
(data / 'user').mkdir(parents=True)


def run(label, guest, directory, failure_stage=None):
    output = case / label
    output.mkdir()
    with (output / 'result.tsv').open('wb') as out, (output / 'stderr.log').open('wb') as err:
        process = subprocess.run(['timeout', '--kill-after=5s', '30s', str(binary),
            str(case / f'{guest}.nro')], cwd=directory, stdout=out, stderr=err)
    log = (directory / 'user/log/eden_log.txt').read_text()
    (output / 'eden_log.txt').write_text(log)
    (output / 'exit-status.txt').write_text(str(process.returncode) + '\n')
    text = (output / 'result.tsv').read_text()
    assert process.returncode == 0, f'Host lifecycle failed: {label}'
    assert game_log_is_clean(log), label
    if failure_stage is None:
        receipt(text, log)
        assert log.count('EDEN_GUEST_SAVE_READ_PASS') == 1
        assert log.count('EDEN_GUEST_SAVE_WRITE_PASS') == (guest == 'write')
    else:
        # Guest failure still exits normally so the host must finish cleanup.
        assert log.count('EDEN_CORE_FIXTURE_FAIL') == 1
        assert 'EDEN_CORE_FIXTURE_PASS' not in log and 'EDEN_GUEST_SAVE_READ_PASS' not in log
        stages = re.findall(r'EDEN_GUEST_FAIL_STAGE=([0-9a-f]{16})', log)
        assert stages == [f'{failure_stage:016x}'], (label, stages)
        assert text == (case / 'writer/result.tsv').read_text()
    print(label, 'PASS', flush=True)


run('writer', 'write', data)
saved = list((data / 'user/nand/user/save').rglob('eden-save-persistence.bin'))
assert len(saved) == 1, saved
expected = bytes((i * 29 + 7) & 255 for i in range(512))
assert saved[0].read_bytes() == expected
relative = saved[0].relative_to(data)
run('reader-process-1', 'read', data)
run('reader-process-2', 'read', data)
assert saved[0].read_bytes() == expected

for label, stage in (('missing', 52), ('truncated', 53), ('corrupt', 54)):
    negative = case / f'data-{label}'
    shutil.copytree(data / 'user', negative / 'user')
    target = negative / relative
    if label == 'missing':
        target.unlink()
    elif label == 'truncated':
        target.write_bytes(expected[:-1])
    else:
        damaged = bytearray(expected)
        damaged[257] ^= 0x80
        target.write_bytes(damaged)
    run(label, 'read', negative, stage)

(case / 'assessment.json').write_text(json.dumps({
    'platform': 'Linux host', 'positive_processes': 3, 'negative_controls': ['missing', 'truncated', 'corrupt'],
    'save_file': str(relative), 'save_sha256': hashlib.sha256(expected).hexdigest(),
    'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    'scope': 'Graceful reopen/restart persistence only; Eden Commit is a success stub.',
}, indent=2) + '\n')
print('Guest save persistence and missing/truncated/corrupt-file rejection PASS')
