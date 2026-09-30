#!/usr/bin/env python3
"""Check game-mode option rejection and early-exit cleanup with the homebrew fixture."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'headless'))
from check import game_log_is_clean, game_lifecycle_is_complete, filesystem_call_counts, game_session_markers_match

assert filesystem_call_counts('[0] Service.FS <Debug> core/fsp/fs_i_storage.cpp:33:Read: called\n'
                              '[1] Service.AM <Debug> core/fsp/fs_i_storage.cpp:33:Read: called') == \
    {'fs_i_storage.cpp::Read': 1}

assert game_log_is_clean('[0.1] Service.AM <Info> called')
for bad_log in ('', ' \n', '[0.1] Debug <Critical> assert failed',
                '[0.1] Service.AM <Info> called\n[0.2] Debug <Critical> assert failed'):
    assert not game_log_is_clean(bad_log), 'Accepted missing log or soft assertion'
binary = root / 'build/headless-host/eden-headless'
fixture = root / 'build/fixture/core-homebrew.nro'
with tempfile.TemporaryDirectory(prefix='eden-game-mode-') as directory:
    case = Path(directory)
    (case / 'user').mkdir()
    for flags in (['--game', '--devices'], ['--game', '--game'],
                  ['--cpu-pressure', '--game'], ['--cpu-pressure', '--devices'],
                  ['--cpu-pressure', '--soak'], ['--cpu-pressure', '--cpu-pressure'],
                  ['--shutdown-sweep', '--game'], ['--shutdown-sweep', '--devices'],
                  ['--shutdown-sweep', '--repeat'], ['--shutdown-sweep', '--soak'],
                  ['--shutdown-sweep', '--cpu-pressure'], ['--shutdown-sweep', '--shutdown-sweep']):
        result = subprocess.run([str(binary), str(fixture), *flags], cwd=case,
                                capture_output=True, timeout=10)
        assert result.returncode == 2, flags
    result = subprocess.run([str(binary), str(fixture), '--game', '--repeat'], cwd=case,
                            capture_output=True, text=True, timeout=45)
    assert result.returncode == 0, result.stderr
    expected = ['core_constructed', 'core_initialized'] + ['game_loaded', 'cpu_manager_ready',
                'guest_exit_callback', 'core_shutdown'] * 3 + ['core_destroyed', 'HEADLESS_COMPLETE']
    assert result.stdout.splitlines() == [name + '\tPASS' for name in expected]
    lines = result.stdout.splitlines()
    assert game_lifecycle_is_complete(lines, 3)
    assert game_lifecycle_is_complete(lines[:6] + lines[-2:], 1)
    for bad in (lines[:-1], lines + lines, lines[:5] + lines[6:],
                [line.replace('core_shutdown\tPASS', 'core_shutdown\tFAIL') for line in lines]):
        assert not game_lifecycle_is_complete(bad, 3)
    mixed = lines.copy()
    mixed[4] = 'game_observation_complete\tPASS'
    assert game_lifecycle_is_complete(mixed, 3)
    mixed[4] = 'game_capture_shutdown\tPASS'
    assert game_lifecycle_is_complete(mixed, 3)
    assert not game_lifecycle_is_complete(lines, 1)
    log = (case / 'user/log/eden_log.txt').read_text()
    assert game_session_markers_match(log, 3)
    for bad_log in ('', log + '\nEDEN_GAME_SESSION_BEGIN 4',
                    log.replace('EDEN_GAME_SESSION_END 2', 'missing')):
        assert not game_session_markers_match(bad_log, 3)
print('Game-mode early exit, cleanup, invalid options, and critical-log rejection PASS')
