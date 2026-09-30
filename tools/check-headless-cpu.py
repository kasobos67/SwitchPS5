#!/usr/bin/env python3
"""Bounded full-core A64 code-cache pressure, with an independent integer oracle."""
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
from check import fixture, receipt, game_log_is_clean

blocks = 65536
mask = (1 << 64) - 1
expected = []
value = 1
for _ in range(2):
    for index in range(blocks):
        for _ in range(8):
            value = (value + index % 4095 + 1) & mask
            value ^= ((value >> 13) | (value << 51)) & mask
    expected.append(value)

case = Path(tempfile.mkdtemp(prefix='guest-cpu-', dir=root / 'results'))
print(case, flush=True)
assembly = case / 'pressure.S'
assembly.write_text(f'''.text
.global cpu_pressure
cpu_pressure:
    stp x19, x20, [sp, #-32]!
    str x30, [sp, #16]
    adr x19, blocks
    mov x20, #{blocks}
1:  blr x19
    add x19, x19, #68
    subs x20, x20, #1
    b.ne 1b
    ldr x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
blocks:
.set index, 0
.rept {blocks}
    .rept 8
        add x0, x0, #((index % 4095) + 1)
        eor x0, x0, x0, ror #13
    .endr
    ret
    .set index, index + 1
.endr
''')

def command(*args):
    subprocess.run(args, cwd=root, check=True)

command('clang-18', '--target=aarch64-none-elf', '-c', str(assembly), '-o', str(case / 'pressure.o'))
command('clang-18', '--target=aarch64-none-elf', '-c', 'fixtures/core-homebrew.S', '-o', str(case / 'header.o'))
for label, answer in (('positive', value), ('wrong-answer', value ^ 1)):
    command('clang-18', '--target=aarch64-none-elf', '-Os', '-ffreestanding', '-fno-builtin',
            '-fno-stack-protector', '-mno-outline-atomics', f'-DEDEN_GUEST_CPU_EXPECTED={answer}ULL',
            '-c', 'fixtures/core-services.c', '-o', str(case / f'{label}.o'))
    command('ld.lld-18', '-T', 'fixtures/core-homebrew.ld', str(case / 'header.o'),
            str(case / f'{label}.o'), str(case / 'pressure.o'), '-o', str(case / f'{label}.elf'))
    command('llvm-objcopy-18', '-O', 'binary', str(case / f'{label}.elf'), str(case / f'{label}.nro'))
    fixture((case / f'{label}.nro').read_bytes(), max_size=10 * 1024**2)

binary = case / 'eden-headless'
shutil.copy2(root / 'build/headless-host/eden-headless', binary)
assessments = []
for label, cycles in (('positive', 3), ('wrong-answer', 1)):
    directory = case / label
    (directory / 'user').mkdir(parents=True)
    with (directory / 'result.tsv').open('wb') as out, (directory / 'stderr.log').open('wb') as err:
        result = subprocess.run(['timeout', '--kill-after=5s', f'{190 * cycles}s', '/usr/bin/time',
            '-f', '%e %M', '-o', str(directory / 'time.txt'), str(binary),
            str(case / f'{label}.nro'), '--cpu-pressure', *(['--repeat'] if cycles == 3 else [])],
            cwd=directory, stdout=out, stderr=err)
    log = (directory / 'user/log/eden_log.txt').read_text()
    text = (directory / 'result.tsv').read_text()
    errors = (directory / 'stderr.log').read_text()
    (directory / 'exit-status.txt').write_text(str(result.returncode) + '\n')
    assert result.returncode == 0, (label, result.returncode)
    assert game_log_is_clean(log), label
    values = re.findall(r'EDEN_GUEST_CPU_RESULT=([0-9a-f]{16})', log)
    assert values == [f'{v:016x}' for v in expected] * cycles, (label, values)
    pressure = re.findall(r'EDEN_JIT_PRESSURE core=(\d+) capacity=(\d+) remaining=(\d+)', errors)
    assert len(pressure) >= 2 * cycles, 'Insufficient actual code-cache pressure'
    assert all(int(capacity) == 16 * 1024**2 and int(remaining) < 1024**2
               for _, capacity, remaining in pressure), pressure
    per_cycle = [part.split('host_heap phase=core_shutdown ')[0].count('EDEN_JIT_PRESSURE ')
                 for part in errors.split('host_heap phase=cpu_manager_ready ')[1:]]
    assert len(per_cycle) == cycles and all(count >= 2 for count in per_cycle), per_cycle
    if label == 'positive':
        receipt(text, log, cycles)
        assert log.count('EDEN_GUEST_CPU_PASS') == cycles
        assert log.count('EDEN_CORE_FIXTURE_PASS') == cycles
        assert 'EDEN_CORE_FIXTURE_FAIL' not in log
    else:
        # A rejected answer still needs a complete, ordinary shutdown receipt.
        positive = (case / 'positive/result.tsv').read_text().splitlines()
        assert text.splitlines() == positive[:6] + positive[-2:]
        assert 'EDEN_GUEST_CPU_PASS' not in log and 'EDEN_CORE_FIXTURE_PASS' not in log
        assert log.count('EDEN_CORE_FIXTURE_FAIL') == 1
        assert re.findall(r'EDEN_GUEST_FAIL_STAGE=([0-9a-f]{16})', log) == [f'{60:016x}']
    seconds, rss = (directory / 'time.txt').read_text().split()
    shutdown_heap = [int(v) for v in re.findall(r'host_heap phase=core_shutdown allocated_bytes=(\d+)', errors)]
    assessments.append(dict(label=label, cycles=cycles, cache_evacuations=len(pressure),
                            evacuations_per_cycle=per_cycle, shutdown_heap_bytes=shutdown_heap,
                            wall_seconds=float(seconds), peak_rss_kib=int(rss)))
    print(label, 'PASS', assessments[-1], flush=True)

(case / 'assessment.json').write_text(json.dumps(dict(
    platform='Linux host', blocks=blocks, rounds_per_block=8, passes_per_session=2,
    expected_results=[f'{v:016x}' for v in expected], runs=assessments,
    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    fixture_sha256=hashlib.sha256((case / 'positive.nro').read_bytes()).hexdigest(),
    scope='A64 arithmetic and automatic JIT cache evacuation; not PS5 performance or all CPU instructions.'
), indent=2) + '\n')
print('CPU correctness under JIT cache pressure PASS')
