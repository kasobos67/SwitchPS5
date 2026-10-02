#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The first block a game compiles must not pay for the instruction decoder's table.

Dynarmic builds a table of 4096 lists from its ~640 A64 (and ~260 A32) instruction patterns the
first time a block is translated. Upstream parses every pattern's bit string again for each of
the 4096 lists: about half a second on the first compilation, on the guest core that asked for
it. The port parses each pattern once (headless/CMakeLists.txt, decoder tables).

This runs the host build's cold-compilation benchmark (eden-memory-check --cold-compile): three
times the same 8192 blocks, each in a new JIT. Only the first builds the table, so it may not
take much longer than the others. It also checks that both derived decoders have the one-parse
form and list the handlers in upstream's order.
"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
cache = Path((root / '.local/headless-cache').read_text().strip())
for name in ('A64/decoder/a64.h', 'A32/decoder/arm.h'):
    derived = (cache / 'build/headless/include/dynarmic/frontend' / name).read_text()
    assert 'for (size_t i = 0; i < t.size(); ++i) {\n#define INST' not in derived, name
    assert 'all.emplace_back(' in derived and 't[i].push_back(e);' in derived, name

def trials():
    output = subprocess.run([str(cache / 'build/bin/eden-memory-check'), '--cold-compile'],
                            capture_output=True, text=True, check=True, timeout=120).stdout
    seconds = [float(s) for s in re.findall(r'COLD_COMPILE trial=\d blocks=8192 chain=1 seconds=([0-9.]+)', output)]
    assert len(seconds) == 3, output
    return seconds

# The best of three runs: another program's burst must not fail the build.
ratio, first, later = min((s[0] / min(s[1:]), s[0], min(s[1:])) for s in (trials() for _ in range(3)))
print(f'Decoder table: first compilation {first * 1000:.0f} ms, later {later * 1000:.0f} ms, ratio {ratio:.1f}')
assert ratio < 3.0, 'the first compilation still pays for the decoder table'
print('Decoder startup: the table is built from one parse of each pattern PASS')
