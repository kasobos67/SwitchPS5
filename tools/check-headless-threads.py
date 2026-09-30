#!/usr/bin/env python3
"""Full-core guest thread checks; --lifecycle adds FP/SIMD state and guest memory."""
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

assert sys.argv[1:] in ([], ['--lifecycle'])
lifecycle = bool(sys.argv[1:])
name = 'core-lifecycle' if lifecycle else 'core-threads'
case = Path(tempfile.mkdtemp(prefix='guest-lifecycle-' if lifecycle else 'guest-threads-', dir=root / 'results'))
print(case, flush=True)
binary = case / 'eden-headless'
shutil.copy2(root / 'build/headless-host/eden-headless', binary)
disassembly = subprocess.check_output(['llvm-objdump-18', '-d',
    str(root / f'build/fixture/{name}.elf')], text=True)
operations = ('frinti', 'ldar', 'stlr') if lifecycle else ('ldaxr', 'stlxr', 'ldar', 'stlr')
assert all(re.search(r'\b' + op + r'\b', disassembly) for op in operations)
(case / 'disassembly.txt').write_text(disassembly)
runs = []
for label, cycles in ((name, 20), (name + '-wrong', 1)):
    guest = case / f'{label}.nro'
    shutil.copy2(root / f'build/fixture/{label}.nro', guest)
    directory = case / label
    (directory / 'user').mkdir(parents=True)
    with (directory / 'result.tsv').open('wb') as out, (directory / 'stderr.log').open('wb') as err:
        result = subprocess.run(['timeout', '--kill-after=5s', '120s', '/usr/bin/time',
            '-f', '%e %M', '-o', str(directory / 'time.txt'), str(binary), str(guest),
            *(['--soak'] if cycles == 20 else [])], cwd=directory, stdout=out, stderr=err)
    (directory / 'exit-status.txt').write_text(str(result.returncode) + '\n')
    text = (directory / 'result.tsv').read_text()
    log = (directory / 'user/log/eden_log.txt').read_text()
    errors = (directory / 'stderr.log').read_text()
    assert result.returncode == 0 and game_log_is_clean(log), label
    fields = (('STATE_ERRORS', [0, 0]), ('MEMORY_ERROR', [0xca01, 0xcc01, 0xca01, 0xd401])) if lifecycle else (
        ('THREAD_COUNT', [40000]), ('THREAD_SUM', [40000 * 39999 // 2]),
        ('THREAD_CORES', [i << 32 | i for i in range(4)]))
    for field, expected in fields:
        actual = re.findall(r'EDEN_GUEST_' + field + r'=([0-9a-f]{16})', log)
        assert actual == [f'{v:016x}' for v in expected] * cycles, (label, field, actual)
    if lifecycle:
        assert log.count('EDEN_GUEST_STATE_PASS') == cycles
        assert log.count('EDEN_GUEST_MEMORY_PASS') == cycles
    if cycles == 20:
        receipt(text, log, cycles)
        if not lifecycle: assert log.count('EDEN_GUEST_THREADS_PASS') == cycles
    else:
        positive = (case / name / 'result.tsv').read_text().splitlines()
        assert text.splitlines() == positive[:6] + positive[-2:]
        assert log.count('EDEN_CORE_FIXTURE_FAIL') == 1
        assert 'EDEN_GUEST_THREADS_PASS' not in log and 'EDEN_CORE_FIXTURE_PASS' not in log
        assert re.findall(r'EDEN_GUEST_FAIL_STAGE=([0-9a-f]{16})', log) == [f'{83 if lifecycle else 63:016x}']
    seconds, rss = (directory / 'time.txt').read_text().split()
    heap = [int(v) for v in re.findall(r'host_heap phase=core_shutdown allocated_bytes=(\d+)', errors)]
    assert len(heap) == cycles
    runs.append(dict(label=label, cycles=cycles, wall_seconds=float(seconds), peak_rss_kib=int(rss),
                     shutdown_heap_bytes=heap, fixture_sha256=hashlib.sha256(guest.read_bytes()).hexdigest()))
    print(label, 'PASS', flush=True)
(case / 'assessment.json').write_text(json.dumps(dict(platform='Linux host', fixture=name,
    runs=runs, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    scope='Selected guest thread/memory lifecycle checks; not exhaustive memory ordering or PS5 qualification.'
), indent=2) + '\n')
print(name, 'and deliberate failure rejection PASS')
