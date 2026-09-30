"""Build the real generated SlotVector with a throwing resource and worker error."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path, help='Pinned Eden source directory')
parser.add_argument('--build', type=Path, help='Verify actual Ninja object header dependencies')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
if args.build:
    dependencies = subprocess.check_output(['ninja', '-C', str(args.build), '-t', 'deps'], text=True)
    expected = str(args.build / 'headless/recovery/common/slot_vector.h')
    headers = [line.strip() for line in dependencies.splitlines()
               if line.strip().endswith('/common/slot_vector.h')]
    assert headers and all(header == expected for header in headers), 'Build used unpatched SlotVector'
    print(f'All {len(headers)} SlotVector consumers use the recovery header')
with tempfile.TemporaryDirectory(prefix='eden-recovery-') as directory:
    out = Path(directory)
    (out / 'common').mkdir()
    # Only diagnostic macros are substituted; container/types are real Eden code.
    (out / 'common/assert.h').write_text('#pragma once\n#include <cassert>\n#define DEBUG_ASSERT(x) assert(x)\n')
    subprocess.run(['cmake', '-DSLOT_VECTOR_INPUT=' + str(args.source / 'src/common/slot_vector.h'),
                    '-DSLOT_VECTOR_OUTPUT=' + str(out / 'common/slot_vector.h'),
                    '-P', str(root / 'headless/slot_vector_recovery.cmake')], check=True)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(out),
                    '-I' + str(args.source / 'src'), '-I' + str(root / 'headless'),
                    str(root / 'headless/slot_vector_recovery_check.cpp'), '-o', str(out / 'check')], check=True)
    subprocess.run([str(out / 'check')], check=True)
print('Slot rollback and GPU exception handoff: ASan/UBSan PASS')
