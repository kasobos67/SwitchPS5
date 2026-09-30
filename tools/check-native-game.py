#!/usr/bin/env python3
"""Require native lifecycle, real guest drawing, presentation and nonuniform capture."""
from pathlib import Path
import re
import json
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'headless'))
from check import game_lifecycle_is_complete, game_log_is_clean, game_session_markers_match, graphics, native_heap


def frame_evidence(log, heap, image):
    assert not re.search(r'(?:texture-prepare|resource-prepare) reject=|draw-rejected', heap), 'Driver rejected guest resources or draw'
    assert 'Failed to build shader' not in log and 'linking with uncompiled' not in log, 'Shader compilation failed'
    draws = re.findall(r'EDEN_GUEST_GL_DRAW count=(\d+) error=(\d+)', log)
    assert draws and all(error == '0' for _, error in draws), 'Missing or failed guest GL draw'
    header, dimensions, maximum, pixels = image.split(b'\n', 3)
    width, height = map(int, dimensions.split())
    assert header == b'P6' and maximum == b'255' and 0 < width <= 1920 and 0 < height <= 1080
    assert len(pixels) == width * height * 3, 'Incomplete capture'
    assert any(pixels[i:i+3] != pixels[:3] for i in range(0, len(pixels), 3)), 'Uniform capture is inconclusive'
    assert f'EDEN_GAME_CAPTURE completed=1 width={width} height={height}' in heap


if sys.argv[1:] == ['--selftest']:
    receipt = '\n'.join(f'phase{i}\tPASS' for i in range(6))
    heap_samples = '\n'.join(
        f'[ps5-opengl-heap] phase=phase{i} state=2 live_bytes={100 if i < 5 else 1} '
        f'peak_bytes={900 * 1024**2} failures=0 ambiguous_zero_reallocs=0'
        for i in range(6))
    native_heap(receipt, heap_samples, b'')
    for peak in (1024 * 1024**2, 1024 * 1024**2 + 1):
        try:
            native_heap(receipt, heap_samples.replace(str(900 * 1024**2), str(peak)), b'')
        except AssertionError:
            continue
        raise AssertionError('Accepted native heap at or above the frozen 1 GiB ceiling')
    expanded = heap_samples.replace(str(900 * 1024**2), str(1900 * 1024**2))
    native_heap(receipt, expanded, b'', 2048 * 1024**2)
    for peak in (2048 * 1024**2, 2048 * 1024**2 + 1):
        try:
            native_heap(receipt, heap_samples.replace(str(900 * 1024**2), str(peak)), b'', 2048 * 1024**2)
        except AssertionError:
            continue
        raise AssertionError('Accepted heap at or above frozen 2048MiB budget')
    log = 'EDEN_GUEST_GL_DRAW count=1 error=0'
    heap = 'EDEN_GAME_CAPTURE completed=1 width=2 height=1'
    image = b'P6\n2 1\n255\n\x00\x00\x00\xff\x00\x00'
    frame_evidence(log, heap, image)
    for bad in [('', heap, image), (log + 'Failed to build shader', heap, image),
                (log + 'linking with uncompiled shader', heap, image),
                (log.replace('error=0', 'error=1282'), heap, image),
                (log, '', image), (log, heap + '\ntexture-prepare reject=layout', image),
                (log, heap + '\nresource-prepare reject=fragment-textures', image),
                (log, heap + '\ndraw-rejected status=-15', image), (log, heap, image[:-1]),
                (log, heap, image[:-6] + bytes(6))]:
        try:
            frame_evidence(*bad)
        except AssertionError:
            continue
        raise AssertionError('Accepted missing, invalid or blank game rendering evidence')
    print('Guest drawing/presentation/capture rejection controls PASS')
else:
    folder = Path(sys.argv[1])
    receipt = (folder / 'result.tsv').read_text()
    log = (folder / 'user/log/eden_log.txt').read_text()
    heap = (folder / 'heap.log').read_text()
    assert game_lifecycle_is_complete(receipt.splitlines(), 1)
    assert game_log_is_clean(log) and game_session_markers_match(log, 1)
    candidate = folder.parent / 'HEADLESS_CANDIDATE.json'
    heap_limit = json.loads(candidate.read_text())['data_heap_bytes'] if candidate.exists() else 1024 * 1024**2
    native_heap(receipt, heap, (folder / 'stderr.log').read_bytes(), heap_limit)
    graphics(heap, log, 1)
    frame_evidence(log, heap, (folder / 'game-frame.ppm').read_bytes())
    print('NATIVE_GAME_FIRST_FRAME_PASS (gameplay and performance unqualified)')
