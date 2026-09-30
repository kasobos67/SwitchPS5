#!/usr/bin/env python3
"""Execute the JIT assertion macro with successful and failed conditions."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
macro = (root / 'headless/jit_assert.inc').read_text()
source = r'''
#include <cassert>
#define YUZU_NO_INLINE __attribute__((noinline))
static int evaluated, formatted, logged, failed;
static int argument() { return ++formatted; }
static void log(const char*, int) { ++logged; }
#define LOG_CRITICAL(category, ...) log(__VA_ARGS__)
static void AssertFailSoftImpl() { ++failed; }
''' + macro + r'''
int main() {
    ASSERT_MSG(++evaluated == 1, "{}", argument());
    assert(evaluated == 1 && !formatted && !logged && !failed);
    ASSERT_MSG(++evaluated == 0, "{}", argument());
    assert(evaluated == 2 && formatted == 1 && logged == 1 && failed == 1);
    // The upstream soft-failure handler permits continuation.
    ASSERT_MSG(++evaluated == 3, "{}", argument());
    assert(evaluated == 3 && formatted == 1 && logged == 1 && failed == 1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / 'check'
    subprocess.run(['c++', '-std=c++20', '-O3', '-Wall', '-Wextra', '-Werror',
                    '-x', 'c++', '-', '-o', str(executable)],
                   input=source, text=True, check=True)
    subprocess.run([str(executable)], check=True)
print('JIT assertions PASS: single evaluation, lazy formatting, failure and continuation')
