#!/usr/bin/env python3
"""Exact guest FP/SIMD results, including fused rounding and FPCR resume behavior."""
from fractions import Fraction
from pathlib import Path
import hashlib
import json
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'headless'))
from check import receipt, game_log_is_clean

def bits(value, width=32):
    return int.from_bytes(struct.pack('<f' if width == 32 else '<d', value), 'little')

expected = []
for width, precision in ((32, 23), (64, 52)):
    exact_fma = (1 + Fraction(1, 2**precision)) * (1 - Fraction(1, 2**precision)) - 1
    assert exact_fma == -Fraction(1, 2**(2 * precision))
    expected += [bits(v, width) for v in (3.75, -1.5, 3.5, 2, 3.25, -0.0)]
    expected += [2, bits(float(exact_fma), width), bits(math.inf, width)]
for rounding in (round, math.ceil, math.floor, math.trunc):
    expected += [bits(rounding(v)) for v in (2.5, -2.5)]
left, right = [0xffffffff, 0x80000000, 7, 123], [1, 2, 9, 456]
expected += [(a + b) & 0xffffffff for a, b in zip(left, right)]
expected += [(a * b) & 0xffffffff for a, b in zip(left, right)]
expected += [bits(a * b) for a, b in zip([1.5, -2, .25, 8], [2, .5, 4, -.25])]
expected += [left[i] for i in (1, 0, 3, 2)]
expected += [bits(5.625), 0, bits(1.5), 0, bits(5.625, 64), 0, 0, 0]
assert len(expected) == 50

case = Path(tempfile.mkdtemp(prefix='guest-fp-', dir=root / 'results'))
print(case, flush=True)
binary = case / 'eden-headless'
shutil.copy2(root / 'build/headless-host/eden-headless', binary)
disassembly = subprocess.check_output(['llvm-objdump-18', '-d',
    str(root / 'build/fixture/core-fp.elf')], text=True)
for op in ('fadd', 'fmul', 'fdiv', 'fsqrt', 'fmadd', 'fneg', 'frinti'):
    assert re.search(r'\b' + op + r'\s+s', disassembly), op
    if op != 'frinti':
        assert re.search(r'\b' + op + r'\s+d', disassembly), op
for op in ('add', 'mul', 'fmul', 'rev64'):
    assert re.search(r'\b' + op + r'\s+v', disassembly), op
(case / 'disassembly.txt').write_text(disassembly)
runs = []
for label, cycles in (('core-fp', 20), ('core-fp-wrong', 1)):
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
    assert result.returncode == 0 and game_log_is_clean(log), label
    actual = re.findall(r'EDEN_GUEST_FP_RESULT=([0-9a-f]{16})', log)
    assert actual == [f'{v:016x}' for v in expected] * cycles, (label, actual)
    if cycles == 20:
        receipt(text, log, cycles)
        assert log.count('EDEN_GUEST_FP_PASS') == cycles
    else:
        positive = (case / 'core-fp/result.tsv').read_text().splitlines()
        assert text.splitlines() == positive[:6] + positive[-2:]
        assert log.count('EDEN_CORE_FIXTURE_FAIL') == 1
        assert 'EDEN_GUEST_FP_PASS' not in log and 'EDEN_CORE_FIXTURE_PASS' not in log
        assert re.findall(r'EDEN_GUEST_FAIL_STAGE=([0-9a-f]{16})', log) == [f'{73:016x}']
    seconds, rss = (directory / 'time.txt').read_text().split()
    runs.append(dict(label=label, cycles=cycles, wall_seconds=float(seconds), peak_rss_kib=int(rss),
                     fixture_sha256=hashlib.sha256(guest.read_bytes()).hexdigest()))
    print(label, 'PASS', flush=True)
(case / 'assessment.json').write_text(json.dumps(dict(platform='Linux host', results_per_session=len(expected),
    expected_bits=[f'{v:016x}' for v in expected], runs=runs,
    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    scope='Selected scalar FP, FPCR rounding after resume, integer/FP SIMD and lane ordering; not exhaustive IEEE/ARM conformance or PS5 qualification.'
), indent=2) + '\n')
print('Guest floating-point/SIMD and wrong-result rejection PASS')
