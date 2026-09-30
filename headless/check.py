#!/usr/bin/env python3
"""Validate the generated NRO or a complete full-core homebrew receipt."""
from pathlib import Path
from collections import Counter
import re
import struct
import sys


def game_log_is_clean(log):
    """A completed lifecycle must not hide soft assertions or missing logs."""
    return bool(log.strip()) and '<Critical>' not in log


def filesystem_call_counts(log):
    return dict(Counter(file + '::' + function for file, function in re.findall(
        r'Service\.FS <\w+> [^\n]*[/\\]([^/\\:\n]+):\d+:(\w+):', log)))


def game_session_markers_match(log, cycles):
    return re.findall(r'EDEN_GAME_SESSION_(BEGIN|END) (\d+)', log) == [
        (action, str(i)) for i in range(1, cycles + 1) for action in ('BEGIN', 'END')]


def game_lifecycle_is_complete(lines, cycles):
    if cycles not in (1, 3) or len(lines) != 4 + 4 * cycles:
        return False
    if lines[:2] != ['core_constructed\tPASS', 'core_initialized\tPASS'] or \
            lines[-2:] != ['core_destroyed\tPASS', 'HEADLESS_COMPLETE\tPASS']:
        return False
    for index in range(cycles):
        loaded, ready, stopped, shutdown = lines[2 + 4 * index:6 + 4 * index]
        if (loaded, ready, shutdown) != ('game_loaded\tPASS', 'cpu_manager_ready\tPASS', 'core_shutdown\tPASS'):
            return False
        if stopped not in ('guest_exit_callback\tPASS', 'game_observation_complete\tPASS',
                           'game_capture_shutdown\tPASS'):
            return False
    return True


def fixture(data, max_size=5 * 4096):
    assert len(data) % 4096 == 0 and len(data) <= max_size
    assert data[8:24] == b'HOMEBREWNRO0\0\0\0\0'
    assert struct.unpack_from('<I', data)[0] == 0x14000020  # b entry at 0x80
    assert struct.unpack_from('<I', data, 24)[0] == len(data)
    text, text_size, ro, ro_size, rw, rw_size, bss = struct.unpack_from('<7I', data, 32)
    assert text == 0 and text_size == ro and ro + ro_size == rw and rw + rw_size == len(data)
    assert all(x > 0 and x % 4096 == 0 for x in (text_size, ro_size, rw_size, bss))
    assert bss <= 65536 and data[rw:] == bytes(rw_size)


def receipt(text, log, cycles=1):
    assert cycles in (1, 3, 20)
    expected = (['session_start', 'core_constructed', 'core_initialized'] +
                ['nro_loaded', 'cpu_manager_ready', 'guest_exit_callback', 'core_shutdown'] * cycles +
                ['core_destroyed', 'HEADLESS_COMPLETE'])
    assert text.splitlines() == [name + '\tPASS' for name in expected]
    assert log.count('EDEN_CORE_FIXTURE_PASS') == cycles
    assert 'EDEN_CORE_FIXTURE_FAIL' not in log


def native_heap(text, heap, errors, heap_limit=1024 * 1024**2):
    assert heap_limit in (1024 * 1024**2, 2048 * 1024**2, 3072 * 1024**2), 'Unqualified heap budget'
    assert not errors, 'Native stderr is not empty'
    samples = [dict(re.findall(r'(\w+)=(\w+)', line))
               for line in heap.splitlines() if line.startswith('[ps5-opengl-heap] ')]
    assert [s['phase'] for s in samples] == [line.split('\t')[0] for line in text.splitlines()]
    assert samples, 'Missing native heap samples'
    assert all(s['state'] == '2' and s['failures'] == '0' and
               s['ambiguous_zero_reallocs'] == '0' for s in samples)
    assert all(0 <= int(s['live_bytes']) <= int(s['peak_bytes']) < heap_limit for s in samples)
    assert int(samples[-1]['live_bytes']) < int(samples[4]['live_bytes']), 'Core heap was not reclaimed'


def graphics(heap, log, cycles):
    assert heap.count('EDEN_SOCKET_PRIME write=1 write_errno=0 read=1 read_errno=0 value=90') == 1
    assert heap.count('EDEN_GL_VERSION 4.6 (Compatibility Profile)') == 1
    assert heap.count('EDEN_GL_PROFILE 2') == 1
    assert len(re.findall(r'EDEN_EGL_SURFACE [1-9][0-9]*x[1-9][0-9]*', heap)) == 1
    assert heap.count('EDEN_GL_PRESENTATION_PASS frames=2') == cycles
    assert heap.count('EDEN_EGL_CLOSED') == 1
    assert log.count('GL_VERSION: 4.6 (Compatibility Profile)') == cycles
    assert '<Critical>' not in log


def program_ids(log, cycles):
    ids = re.findall(r'Applied game settings for title ID ([0-9A-F]{16}) ', log)
    assert ids == ['0000000000000000'] * cycles, ids


def integration(log, cycles):
    for marker in ('STATE_PASS', 'MEMORY_PASS', 'THREADS_PASS', 'FP_PASS'):
        assert log.count('EDEN_GUEST_' + marker) == cycles, marker
    for field, expected in (('STATE_ERRORS', [0, 0]),
            ('MEMORY_ERROR', [0xca01, 0xcc01, 0xca01, 0xd401]),
            ('THREAD_COUNT', [40000]), ('THREAD_SUM', [799980000]),
            ('THREAD_CORES', [i << 32 | i for i in range(4)])):
        assert re.findall(r'EDEN_GUEST_' + field + r'=([0-9a-f]{16})', log) == [f'{v:016x}' for v in expected] * cycles, field
    # Each guest FP result is checked bit-for-bit before FP_PASS can be emitted.
    assert len(re.findall(r'EDEN_GUEST_FP_RESULT=[0-9a-f]{16}', log)) == 42 * cycles


def services(log, cycles):
    for name in ('TIMER', 'SYNC', 'SERVICE', 'STORAGE'):
        assert log.count(f'EDEN_GUEST_{name}_PASS') == cycles, name


def renderer(log, cycles):
    assert log.count('EDEN_GUEST_RENDERER_INIT_PASS') == cycles
    assert log.count('EDEN_GUEST_RENDERER_UPDATE_PASS') == cycles * 3
    assert log.count('EDEN_GUEST_RENDERER_PASS') == cycles
    frames = [int(value, 16) for value in re.findall(r'EDEN_GUEST_RENDERER_FRAMES=([0-9a-f]{16})', log)]
    assert len(frames) == cycles * 3
    assert all(frames[i] < frames[i + 1] < frames[i + 2] for i in range(0, len(frames), 3))


def renderer_voice(log, cycles, native=False):
    renderer(log, cycles)
    assert log.count('EDEN_GUEST_RENDERER_VOICE_PASS') == cycles
    samples = [int(value, 16) for value in re.findall(r'EDEN_GUEST_RENDERER_VOICE_SAMPLES=([0-9a-f]{16})', log)]
    assert len(samples) == cycles * 3
    assert all(samples[i] < samples[i + 1] < samples[i + 2] for i in range(0, len(samples), 3))
    if not native:
        assert log.count('EDEN_GUEST_RENDERER_PCM_PASS') == cycles


def renderer_mix(log, cycles):
    assert log.count('EDEN_GUEST_RENDERER_MIX_PASS') == cycles
    samples = [int(value, 16) for value in re.findall(r'EDEN_GUEST_RENDERER_SECOND_SAMPLES=([0-9a-f]{16})', log)]
    assert len(samples) == cycles * 3
    assert all(samples[i] < samples[i + 1] < samples[i + 2] for i in range(0, len(samples), 3))


def guest_devices(log, cycles, native=False, axes=False, directions=False, *, voice=False):
    for name in ('HID_READY', 'HID_PASS', 'AUDIO_PASS'):
        assert log.count(f'EDEN_GUEST_{name}') == cycles, name
    buttons = re.findall(r'EDEN_GUEST_HID_BUTTONS=([0-9a-f]{16})', log)
    assert len(buttons) == cycles and all(int(value, 16) & 1 for value in buttons)
    ports = re.findall(r'EDEN_AUDIO_PORT_CLOSED frames=(\d+) nonzero=(\d+) failed=(\w+) drain=(-?\d+) close=(-?\d+)', log)
    assert len(ports) >= cycles
    assert all(int(frames) >= int(nonzero) and failed == 'false' and int(drain) >= 0 and int(close) >= 0
               for frames, nonzero, failed, drain, close in ports)
    tones = [(int(frames), int(nonzero)) for frames, nonzero, *_ in ports if int(nonzero)]
    if voice:
        rendered = [(frames, nonzero) for frames, nonzero in tones if nonzero != 11999]
        assert len(rendered) == cycles and all(nonzero >= 480 for _, nonzero in rendered)
        tones = [(frames, nonzero) for frames, nonzero in tones if nonzero == 11999]
    assert len(tones) == cycles and all(frames >= 12000 and nonzero == 11999 for frames, nonzero in tones)
    pads = re.findall(r'EDEN_PAD_CLOSED polls=(\d+) samples=(\d+) usable=(\d+) intercepted=(\d+) circle=(\d+) errors=(\d+) last=(-?\d+) close=(-?\d+)', log)
    assert len(pads) == 1
    polls, samples, usable, intercepted, circle, errors, last, close = map(int, pads[0])
    assert polls > 0 and samples >= usable >= circle > 0 and errors == 0 and last >= 0 and close >= 0
    if not native:
        assert log.count('EDEN_GUEST_AUDIO_OUTPUT_PASS') == cycles
    if not native or axes or directions:
        assert all(int(value, 16) == 0x240101 for value in buttons)
        for axis in ('LX', 'RY'):
            values = re.findall(rf'EDEN_GUEST_HID_{axis}=([0-9a-f]{{16}})', log)
            if native and directions:
                assert len(values) == cycles and all(0 < int(value, 16) <= 32767 for value in values)
            else:
                assert values == ['0000000000007fff'] * cycles


if __name__ == '__main__':
    if sys.argv[1] == '--fixture':
        fixture(Path(sys.argv[2]).read_bytes())
        print('Generated NRO layout PASS')
    else:
        folder = Path(sys.argv[1])
        text = (folder / 'result.tsv').read_text()
        log = (folder / 'user/log/eden_log.txt').read_text()
        flags = set(sys.argv[2:])
        assert len(flags) == len(sys.argv[2:]) and flags <= {'--repeat', '--soak', '--native', '--native-axes', '--native-directions', '--renderer', '--renderer-voice', '--renderer-mix', '--renderer-src', '--renderer-high', '--metadata', '--devices', '--services', '--guest-devices', '--integration', '--graphics'}
        assert not ('--soak' in flags and flags & {'--repeat', '--native'})
        assert '--native-axes' not in flags or {'--native', '--guest-devices'} <= flags
        assert '--native-directions' not in flags or {'--native', '--guest-devices'} <= flags
        assert not {'--native-axes', '--native-directions'} <= flags
        assert '--renderer-voice' not in flags or {'--renderer', '--guest-devices'} <= flags
        assert '--renderer-mix' not in flags or '--renderer-voice' in flags
        assert '--renderer-src' not in flags or '--renderer-mix' in flags
        assert '--renderer-high' not in flags or '--renderer-src' in flags
        cycles = 20 if '--soak' in flags else 3 if '--repeat' in flags else 1
        receipt(text, log, cycles)
        if '--graphics' in flags:
            assert '--native' in flags
            graphics_heap = (folder / 'heap.log').read_text()
            graphics(graphics_heap, log, cycles)
            for marker in ('EDEN_SOCKET_PRIME', 'EDEN_GL_PROFILE 2', 'EDEN_GL_PRESENTATION_PASS frames=2', 'EDEN_EGL_CLOSED'):
                try:
                    graphics(graphics_heap.replace(marker, ''), log, cycles)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('Accepted missing graphics evidence: ' + marker)
        if '--integration' in flags:
            integration(log, cycles)
            for bad in (log.replace('EDEN_GUEST_STATE_PASS', ''),
                        log.replace('EDEN_GUEST_THREAD_COUNT=0000000000009c40', 'EDEN_GUEST_THREAD_COUNT=0000000000009c3f')):
                try:
                    integration(bad, cycles)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('Accepted incomplete CPU integration evidence')
        if '--renderer-src' in flags:
            assert log.count('EDEN_GUEST_RENDERER_SRC_PASS') == cycles
        if '--renderer-high' in flags:
            assert log.count('EDEN_GUEST_RENDERER_HIGH_SRC_PASS') == cycles
        if '--renderer-mix' in flags:
            renderer_mix(log, cycles)
            for bad in (log.replace('EDEN_GUEST_RENDERER_MIX_PASS', ''),
                        log + 'EDEN_GUEST_RENDERER_MIX_PASS',
                        re.sub(r'EDEN_GUEST_RENDERER_SECOND_SAMPLES=[0-9a-f]{16}',
                               'EDEN_GUEST_RENDERER_SECOND_SAMPLES=0000000000000000', log)):
                try:
                    renderer_mix(bad, cycles)
                except AssertionError:
                    continue
                raise AssertionError('Accepted missing mix marker or stalled second voice')
        if '--renderer-voice' in flags:
            renderer_voice(log, cycles, '--native' in flags)
            bad_logs = [log.replace('EDEN_GUEST_RENDERER_VOICE_PASS', ''),
                        log + 'EDEN_GUEST_RENDERER_VOICE_PASS',
                        re.sub(r'EDEN_GUEST_RENDERER_VOICE_SAMPLES=[0-9a-f]{16}',
                               'EDEN_GUEST_RENDERER_VOICE_SAMPLES=0000000000000000', log)]
            if '--native' not in flags:
                bad_logs.append(log.replace('EDEN_GUEST_RENDERER_PCM_PASS', ''))
            for bad in bad_logs:
                try:
                    renderer_voice(bad, cycles, '--native' in flags)
                except AssertionError:
                    continue
                raise AssertionError('Accepted missing voice output or stalled voice samples')
        if '--renderer' in flags:
            renderer(log, cycles)
            for marker in ('INIT_', 'UPDATE_', ''):
                name = f'EDEN_GUEST_RENDERER_{marker}PASS'
                for bad in (log.replace(name, ''), log + name):
                    try:
                        renderer(bad, cycles)
                    except AssertionError:
                        continue
                    raise AssertionError('Accepted missing or duplicate renderer coverage')
            try:
                renderer(re.sub(r'EDEN_GUEST_RENDERER_FRAMES=[0-9a-f]{16}',
                                'EDEN_GUEST_RENDERER_FRAMES=0000000000000000', log), cycles)
            except AssertionError:
                pass
            else:
                raise AssertionError('Accepted a stalled renderer')
        if '--services' in flags:
            services(log, cycles)
            assert not (folder / 'user/sdmc/eden-offline-fixture.bin').exists()
            for name in ('TIMER', 'SYNC', 'SERVICE', 'STORAGE'):
                try:
                    services(log.replace(f'EDEN_GUEST_{name}_PASS', ''), cycles)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('Accepted missing guest service coverage')
        if '--devices' in flags:
            assert log.count('EDEN_DEVICE_FRONTEND_PASS') == 1
        if '--guest-devices' in flags:
            guest_devices(log, cycles, '--native' in flags, '--native-axes' in flags, '--native-directions' in flags, voice='--renderer-voice' in flags)
            bad_logs = [log.replace('EDEN_GUEST_HID_PASS', ''),
                        log.replace('EDEN_GUEST_AUDIO_PASS', ''),
                        log.replace('nonzero=11999', 'nonzero=12000'),
                        log.replace('failed=false', 'failed=true'),
                        log.replace('EDEN_PAD_CLOSED', 'MISSING_PAD_CLOSED'),
                        log + 'EDEN_GUEST_HID_PASS']
            if '--native-axes' in flags:
                bad_logs += [log.replace('EDEN_GUEST_HID_LX=0000000000007fff', 'EDEN_GUEST_HID_LX=0000000000000000'),
                             log.replace('EDEN_GUEST_HID_RY=0000000000007fff', 'EDEN_GUEST_HID_RY=0000000000000000'),
                             log.replace('EDEN_GUEST_HID_BUTTONS=0000000000240101', 'EDEN_GUEST_HID_BUTTONS=0000000000240001')]
            if '--native-directions' in flags:
                for axis in ('LX', 'RY'):
                    for value in ('0000000000000000', '0000000000008000', '00000000ffffffff'):
                        bad_logs.append(re.sub(rf'EDEN_GUEST_HID_{axis}=[0-9a-f]{{16}}',
                                               f'EDEN_GUEST_HID_{axis}={value}', log))
                bad_logs.append(log.replace('EDEN_GUEST_HID_BUTTONS=0000000000240101',
                                            'EDEN_GUEST_HID_BUTTONS=0000000000240001'))
            for bad in bad_logs:
                try:
                    guest_devices(bad, cycles, '--native' in flags, '--native-axes' in flags, '--native-directions' in flags, voice='--renderer-voice' in flags)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('Accepted incomplete or failed guest device coverage')
        if '--metadata' in flags:
            program_ids(log, cycles)
            try:
                program_ids(log.replace('ID 0000000000000000 ', 'ID DEADBEEFDEADBEEF '), cycles)
            except AssertionError:
                pass
            else:
                raise AssertionError('Accepted an incorrect metadata-free homebrew ID')
        if '--native' in sys.argv[2:]:
            heap = (folder / 'heap.log').read_text()
            errors = (folder / 'stderr.log').read_bytes()
            native_heap(text, heap, errors)
            for bad_heap, bad_errors in ((heap.replace('failures=0', 'failures=1'), errors),
                                         ('', errors), (heap, b'port error')):
                try:
                    native_heap(text, bad_heap, bad_errors)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('Accepted missing heap telemetry or native errors')
        for bad in (text.replace('nro_loaded\tPASS', 'nro_loaded\tFAIL'),
                    '\n'.join(text.splitlines()[:-1])):
            try:
                receipt(bad, log, cycles)
            except AssertionError:
                pass
            else:
                raise AssertionError('Accepted failed or incomplete core receipt')
        for bad in (log.replace('EDEN_CORE_FIXTURE_PASS', 'EDEN_CORE_FIXTURE_FAIL'),
                    log + 'EDEN_CORE_FIXTURE_PASS'):
            try:
                receipt(text, bad, cycles)
            except AssertionError:
                pass
            else:
                raise AssertionError('Accepted failed or duplicate guest result')
        print('Full-core homebrew receipt and rejection controls PASS')
