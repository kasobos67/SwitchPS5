# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep callback worker shutdown ahead of destruction of its service owners."""
from pathlib import Path
import sys

def check(text):
    assert 'GameSettings::LoadOverrides(params.program_id,' in text
    assert 'GameSettings::LoadOverrides(program_id,' not in text
    assert '    u64 program_id;\n\n' not in text
    body = text.split('    void ShutdownMainProcess() {', 1)[1].split('    bool IsShuttingDown()', 1)[0]
    steps = ('if (audio_core) audio_core->Shutdown();', 'stop_event.request_stop();',
             'kernel.CloseServices();', 'services.reset();', 'audio_core.reset();')
    assert all(body.count(step) == 1 for step in steps)
    offsets = [body.index(step) for step in steps]
    assert offsets == sorted(offsets), 'Audio callbacks outlive service owners'
    # The cheat engine's timer callback asks the service manager for HID: it is destroyed first.
    assert body.index('cheat_engine.reset();') < body.index('kernel.CloseServices();'), \
        'The cheat engine outlives the services'

if __name__ == '__main__':
    fixed, original = (Path(name).read_text() for name in sys.argv[1:])
    check(fixed)
    for bad in (original, fixed.replace('GameSettings::LoadOverrides(params.program_id,',
            'GameSettings::LoadOverrides(program_id,'), fixed.replace('if (audio_core) audio_core->Shutdown();', '').replace(
            'services.reset();', 'services.reset();\nif (audio_core) audio_core->Shutdown();'),
            fixed.replace('        cheat_engine.reset();\n', '', 1)):
        try:
            check(bad)
        except AssertionError:
            continue
        raise AssertionError('Accepted missing or late audio worker shutdown, or a late cheat engine')
    print('Resolved program ID, early audio shutdown and early cheat engine teardown; rejection controls PASS')
