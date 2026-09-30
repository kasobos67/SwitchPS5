#!/usr/bin/env python3
"""Vary guest stop timing; stop at the first assertion/fault with all-thread stacks."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'headless'))
from check import game_log_is_clean, game_session_markers_match

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('game', type=Path)
parser.add_argument('user_directory', type=Path, help='Existing local user data; copied, never modified')
parser.add_argument('--plain', action='store_true', help='Run the emulator without GDB for regression validation')
args = parser.parse_args()
assert args.game.is_file() and (args.user_directory / 'keys/prod.keys').is_file()
assert (args.user_directory / 'nand/system/Contents').is_dir()
case = Path(tempfile.mkdtemp(prefix='shutdown-sweep-', dir=root / 'results'))
print(case, flush=True)
binary = case / 'eden-headless'
shutil.copy2(root / 'build/headless-host/eden-headless', binary)
commands = case / 'capture.gdb'
commands.write_text('''set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
break AssertFailSoftImpl
commands
silent
printf "EDEN_DEBUG_ASSERT_STOP\\n"
thread apply all bt 20
quit 86
end
catch signal SIGSEGV SIGABRT SIGBUS SIGILL SIGINT
commands
silent
printf "EDEN_DEBUG_SIGNAL_STOP\\n"
thread apply all bt 20
quit 87
end
run
printf "EDEN_DEBUG_NORMAL_EXIT %d\\n", $_exitcode
quit $_exitcode
''')

def debug(program, arguments, directory, output, timeout):
    with output.open('w') as stream:
        invocation = [str(program), *arguments]
        if not (args.plain and program == binary):
            invocation = ['gdb', '-q', '-nx', '-batch', '-x', str(commands), '--args', *invocation]
        result = subprocess.run(['timeout', '--signal=INT', '--kill-after=10s', f'{timeout}s', *invocation],
            cwd=directory, stdout=stream, stderr=subprocess.STDOUT)
    return result.returncode, output.read_text(errors='replace')

# Check both capture branches and normal exit with the same GDB command file.
control = case / 'control.cpp'
control.write_text('''#include <cstdlib>
__attribute__((noinline)) void AssertFailSoftImpl() { asm volatile(""); }
int main(int argc, char** argv) {
    if (argc == 1) AssertFailSoftImpl();
    else if (argv[1][0] == 'a') std::abort();
}
''')
subprocess.run(['clang++-18', '-g', '-O0', str(control), '-o', str(case / 'control')], check=True)
for label, arguments, expected in (('assert', [], 86), ('abort', ['abort'], 87), ('normal', ['normal'], 0)):
    status, output = debug(case / 'control', arguments, case, case / f'control-{label}.txt', 15)
    assert status == expected, (label, status)
    assert ('EDEN_DEBUG_NORMAL_EXIT 0' in output) if expected == 0 else ('main (' in output)
print('Debugger assertion/signal/normal-exit controls PASS', flush=True)

reports = []
for label in ('cold', 'warm'):
    directory = case / label
    if label == 'warm':
        shutil.copytree(args.user_directory, directory / 'user')
    else:
        for relative in ('keys', 'nand/system/Contents'):
            shutil.copytree(args.user_directory / relative, directory / 'user' / relative)
    status, output = debug(binary, [str(args.game.resolve()), '--shutdown-sweep'], directory,
                           directory / ('process.txt' if args.plain else 'debugger.txt'), 150)
    (directory / 'exit-status.txt').write_text(str(status) + '\n')
    log_path = directory / 'user/log/eden_log.txt'
    log = log_path.read_text(errors='replace') if log_path.exists() else ''
    lines = re.findall(r'^(?:core_constructed|core_initialized|game_loaded|cpu_manager_ready|guest_exit_callback|game_observation_complete|core_shutdown|core_destroyed|HEADLESS_COMPLETE)\t(?:PASS|FAIL)$', output, re.M)
    expected = ['core_constructed\tPASS', 'core_initialized\tPASS']
    for index in range(6):
        expected += ['game_loaded\tPASS', 'cpu_manager_ready\tPASS', 'STOP', 'core_shutdown\tPASS']
    expected += ['core_destroyed\tPASS', 'HEADLESS_COMPLETE\tPASS']
    normalized = ['STOP' if line in ('guest_exit_callback\tPASS', 'game_observation_complete\tPASS') else line for line in lines]
    delays = [int(v) for v in re.findall(r'EDEN_SHUTDOWN_DELAY_MS (\d+)', log)]
    passed = (status == 0 and (args.plain or 'EDEN_DEBUG_NORMAL_EXIT 0' in output) and
              normalized == expected and game_log_is_clean(log) and game_session_markers_match(log, 6) and
              delays == [0, 1, 10, 100, 1000, 30000])
    reports.append(dict(label=label, exit_code=status, passed=passed, stop_delays_ms=delays,
                        shutdowns=lines.count('core_shutdown\tPASS'),
                        captured_assertion='EDEN_DEBUG_ASSERT_STOP' in output,
                        captured_signal='EDEN_DEBUG_SIGNAL_STOP' in output))
    (case / 'assessment.json').write_text(json.dumps(dict(
        game=args.game.name, debugger=not args.plain,
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), runs=reports,
        scope='Shutdown timing sweep; clean runs cannot resolve an unreproduced historical crash.'
    ), indent=2) + '\n')
    print(label, 'PASS' if passed else 'FAILED; stopped for investigation', flush=True)
    if not passed:
        raise SystemExit(1)
print('Shutdown timing sweep complete; historical fault remains unresolved unless a failing stack was captured.')
