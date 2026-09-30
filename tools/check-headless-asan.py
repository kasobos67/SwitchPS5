#!/usr/bin/env python3
"""Verify ASan's detector and the instrumented homebrew lifecycle before game testing."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
case = Path(tempfile.mkdtemp(prefix='asan-controls-', dir=root / 'results'))
print(case, flush=True)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:detect_stack_use_after_return=0:'
           'halt_on_error=1:external_symbolizer_path=/usr/bin/llvm-symbolizer-18')
source = case / 'detector.c'
source.write_text('#include <stdlib.h>\nint main(void) { volatile char *p = malloc(8); '
                  'free((void *)p); return p[0]; }\n')
subprocess.run(['clang-18', '-O0', '-g', '-fsanitize=address', str(source),
                '-o', str(case / 'detector')], check=True)
control = subprocess.run([str(case / 'detector')], env=env, capture_output=True, timeout=15)
(case / 'detector.log').write_bytes(control.stderr)
assert control.returncode != 0 and b'AddressSanitizer: heap-use-after-free' in control.stderr
(case / 'user').mkdir()
shutil.copyfile(root / 'build/headless-asan/eden-headless', case / 'eden-headless')
(case / 'eden-headless').chmod(0o700)
with (case / 'result.tsv').open('wb') as out, (case / 'stderr.log').open('wb') as err:
    result = subprocess.run(['timeout', '--kill-after=5s', '60s', './eden-headless',
        str(root / 'build/fixture/core-homebrew.nro'), '--repeat'],
        cwd=case, env=env, stdout=out, stderr=err)
(case / 'exit-status.txt').write_text(str(result.returncode) + '\n')
assert result.returncode == 0, f'ASan fixture failed; inspect {case}'
subprocess.run(['python3', '-B', str(root / 'headless/check.py'), str(case), '--repeat',
                '--metadata', '--services'], check=True)
assert 'ERROR: AddressSanitizer' not in (case / 'stderr.log').read_text()
print('ASan intentional-use-after-free detection and three fixture lifecycles PASS')
