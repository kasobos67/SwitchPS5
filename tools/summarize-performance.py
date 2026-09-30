#!/usr/bin/env python3
"""Summarize cumulative Eden snapshots without counting repeated frames twice."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import tempfile

FRAME = re.compile(r'EDEN_(?:DEV|GAME|VULKAN)_FRAME frames=(\d+) seconds=([\d.]+) fps=([\d.]+) worst_ms=([\d.]+) total=(\d+)')
FIELDS = ('workload', 'scene', 'replay', 'resolution', 'cache', 'profiling')


def latest(run, suffix):
    # Snapshots are cumulative. A later empty file means sandbox unmounted,
    # not zero activity. Never concatenate every repeated snapshot.
    files = [p for p in run.glob('*' + suffix) if p.stat().st_size]
    def order(p):
        prefix = p.name.split('-', 1)[0]
        return int(prefix) if prefix.isdigit() else -1
    return max(files, key=order).read_text(errors='replace') if files else ''


def summarize(run, start, end):
    heap = latest(run, 'heap.log')
    stderr = latest(run, 'stderr.log')
    klog_path = run / 'klog.txt'
    klog = klog_path.read_text(errors='replace') if klog_path.exists() else ''
    elapsed, rows, seen = 0.0, [], set()
    previous_total, frame_resets = 0, 0
    for match in FRAME.finditer(heap):
        frames, seconds, fps, worst, total = map(float, match.groups())
        if total < previous_total:
            frame_resets += 1
        previous_total = total
        if total in seen:
            continue
        seen.add(total)
        if seconds <= 0 or frames <= 0:
            continue
        before = elapsed
        elapsed += seconds
        if before >= start and elapsed <= end:
            rows.append((frames, seconds, fps, worst))
    metadata_path = run / 'measurement.json'
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else {}
    if not isinstance(metadata, dict):
        raise ValueError('measurement.json must contain an object')
    errors = sorted(set(re.findall(r'EDEN_DEV_GL_ERROR .*?message=(.*)', heap)))
    failures = re.findall(r'\[ps5-gallium\] resource-(?:allocate|map)-failed[^\n]*', heap)
    backend_launches = re.findall(r'\[ProsperoEden\] launch: (OpenGL|Vulkan)\s*$', stderr, re.M)
    renderer_failures = re.findall(r'(?:\[ProsperoEden\] session failed:[^\n]*|VK_ERROR_[A-Z_]+)', heap + '\n' + stderr)
    stages = [(backend, phase, int(ns)) for backend, phase, ns in re.findall(
        r'EDEN_STAGE backend=(\w+) phase=(\w+) mono_ns=(\d+)', heap)]
    shader_times = [(int(hit), int(ns)) for hit, ns in re.findall(
        r'PS5VK_RUNTIME_GRAPHICS_CACHE rc=0 hit=([01])[^\n]*?acquire_wall_ns=(\d+) timing_valid=1', stderr)]
    display_times = [(int(fence), int(event)) for fence, event in re.findall(
        r'PS5VK_VIDEO_PRESENTED[^\n]*?fence_wait_ns=(\d+) event_wait_ns=(\d+) timing_valid=1', stderr)]
    counters = []
    for tag in ('ps5-cpu-flush-summary', 'ps5-submit-perf', 'ps5-prepare-perf', 'ps5-batch-summary'):
        values = re.findall(r'\[' + tag + r'\][^\n]*', heap + '\n' + stderr)
        if values:
            counters.append(values[-1])
    owner = re.findall(r'EDEN_PERF_GPU_FRAME frame=\d+ mono_ns=(\d+) cpu_ns=(\d+)', heap)
    cpu = None
    if len(owner) > 1:
        wall = int(owner[-1][0]) - int(owner[0][0])
        if wall > 0:
            cpu = 100 * (int(owner[-1][1]) - int(owner[0][1])) / wall
    fps_values = [r[2] for r in rows]
    return dict(run=str(run), metadata=metadata, windows=len(rows),
                seconds=sum(r[1] for r in rows),
                fps=sum(r[0] for r in rows) / sum(r[1] for r in rows) if rows else None,
                minimum=min(fps_values) if rows else None,
                median=statistics.median(fps_values) if rows else None,
                worst=max(r[3] for r in rows) if rows else None,
                errors=errors, allocation_failures=len(failures),
                backend_launches=backend_launches, frame_resets=frame_resets,
                renderer_failures=sorted(set(renderer_failures)),
                stages=stages,
                shader_times=shader_times, display_times=display_times,
                filesystem_full=len(re.findall('filesystem full', klog, re.I)),
                gpu_cpu_percent=cpu, counters=counters,
                completed='HEADLESS_COMPLETE' in heap or 'HEADLESS_COMPLETE' in latest(run, 'result.tsv'))


def print_summary(s):
    print(Path(s['run']).name)
    for hit, label in ((0, 'miss/compile'), (1, 'hit')):
        times = [ns for found_hit, ns in s['shader_times'] if found_hit == hit]
        if times:
            print(f'  Shader-cache {label}: {len(times)} receipts, acquisition wall total {sum(times)/1e6:.3f}ms; max {max(times)/1e6:.3f}ms')
    if s['display_times']:
        print(f"  Display: {len(s['display_times'])} receipts; fence-wait total {sum(f for f, _ in s['display_times'])/1e6:.3f}ms; flip-event-wait total {sum(e for _, e in s['display_times'])/1e6:.3f}ms")
    session_start = None
    for backend, phase, ns in s['stages']:
        if phase == 'session_start':
            session_start = (backend, ns)
        if session_start and backend == session_start[0] and ns >= session_start[1]:
            print(f'  Stage {backend}/{phase}: {(ns - session_start[1]) / 1e9:.3f}s from session start')
    if s['windows']:
        print(f"  {s['windows']} windows / {s['seconds']:.1f}s: weighted FPS {s['fps']:.2f}; "
              f"window minimum/median {s['minimum']:.2f}/{s['median']:.2f}; worst frame {s['worst']:.1f}ms")
    else:
        print('  No complete frame windows within requested interval.')
    print(f"  Rendering: {s['metadata'].get('rendering', 'unverified')}; "
          f"completion receipt: {s['completed']}; allocation failure reports: {s['allocation_failures']}; "
          f"filesystem-full reports: {s['filesystem_full']}")
    if s['gpu_cpu_percent'] is not None:
        print(f"  GPU-worker CPU/wall across available owner samples: {s['gpu_cpu_percent']:.1f}%")
    if s['frame_resets'] or len(s['backend_launches']) > 1:
        print('  Multiple sessions/frame-counter resets: unsuitable for a single-run comparison.')
    for error in s['renderer_failures'][:4]:
        print('  Renderer/session failure:', error)
    for error in s['errors'][:8]:
        print('  GL:', error)
    if len(s['errors']) > 8:
        print(f"  ... {len(s['errors']) - 8} more distinct GL messages (see logs)")
    if s['completed']:
        for counter in s['counters']:
            print(' ', counter)
    else:
        print('  Final driver totals unavailable; ignoring possible startup-oracle summaries.')


def comparable(a, b):
    return (all(a['metadata'].get(k) and a['metadata'].get(k) == b['metadata'].get(k) for k in FIELDS)
            and all(s['metadata'].get('timing_valid', True) and
                    s['metadata'].get('cache') not in ('unknown', 'uncontrolled-persistent')
                    for s in (a, b))
            and all(s['metadata'].get('rendering') == 'verified' and s['windows']
                    and not s['frame_resets'] and len(s['backend_launches']) <= 1
                    and not s['renderer_failures'] and not s['errors']
                    and not s['allocation_failures'] and not s['filesystem_full'] for s in (a, b)))


def self_test():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        first = 'EDEN_DEV_FRAME frames=10 seconds=1.0 fps=10.0 worst_ms=120.0 total=10\n'
        second = 'EDEN_DEV_FRAME frames=60 seconds=2.0 fps=30.0 worst_ms=50.0 total=70\n'
        (root / '15-heap.log').write_text(first)
        (root / '30-heap.log').write_text(first + first + second + '\npartial EDEN_DEV_FRAME frames=')
        (root / '45-heap.log').write_text('')
        result = summarize(root, 0, 10)
        assert result['windows'] == 2 and math.isclose(result['fps'], 70 / 3)
        assert result['worst'] == 120 and not comparable(result, result)
        assert summarize(root, 1, 3)['fps'] == 30
        assert summarize(root, 4, 5)['fps'] is None
        metadata = {k: 'fixed' for k in FIELDS} | {'rendering': 'verified'}
        (root / 'measurement.json').write_text(json.dumps(metadata))
        result = summarize(root, 0, 10)
        assert comparable(result, result)
        changed = dict(result, metadata=metadata | {'cache': 'different'})
        assert not comparable(result, changed)
        assert not comparable(result, dict(result, allocation_failures=1))
        assert not comparable(result, dict(result, metadata=metadata | {'timing_valid': False}))
        uncontrolled = dict(result, metadata=metadata | {'cache': 'uncontrolled-persistent'})
        assert not comparable(uncontrolled, uncontrolled)
        (root / '30-heap.log').write_text((first + second).replace('EDEN_DEV_FRAME', 'EDEN_GAME_FRAME'))
        assert math.isclose(summarize(root, 0, 10)['fps'], 70 / 3)
        (root / '30-heap.log').write_text((first + second).replace('EDEN_DEV_FRAME', 'EDEN_VULKAN_FRAME'))
        assert math.isclose(summarize(root, 0, 10)['fps'], 70 / 3)
        (root / '30-stderr.log').write_text('[ProsperoEden] launch: Vulkan\n[ProsperoEden] launch: OpenGL\n')
        assert not comparable(result, summarize(root, 0, 10))
        (root / '30-stderr.log').write_text('VK_ERROR_FORMAT_NOT_SUPPORTED\n')
        assert not comparable(result, summarize(root, 0, 10))
        (root / '30-stderr.log').write_text('')
        (root / '30-heap.log').write_text(first + second + first)
        assert not comparable(result, summarize(root, 0, 10))
        (root / '30-heap.log').write_text('EDEN_STAGE backend=Vulkan phase=session_start mono_ns=100\n'
                                       'EDEN_STAGE backend=Vulkan phase=first_frame_callback mono_ns=200\n')
        assert summarize(root, 0, 10)['stages'] == [('Vulkan', 'session_start', 100), ('Vulkan', 'first_frame_callback', 200)]
        (root / '30-stderr.log').write_text(
            'PS5VK_RUNTIME_GRAPHICS_CACHE rc=0 hit=0 compiled_pairs=1 acquire_wall_ns=100 timing_valid=1\n'
            'PS5VK_RUNTIME_GRAPHICS_CACHE rc=0 hit=1 compiled_pairs=1 acquire_wall_ns=2 timing_valid=1\n'
            'PS5VK_RUNTIME_GRAPHICS_CACHE rc=0 hit=1 compiled_pairs=1 acquire_wall_ns=999 timing_valid=0\n'
            'PS5VK_VIDEO_PRESENTED token=1 fence_wait_ns=10 event_wait_ns=20 timing_valid=1\n')
        timing = summarize(root, 0, 10)
        assert timing['shader_times'] == [(0, 100), (1, 2)] and timing['display_times'] == [(10, 20)]
        (root / 'klog.txt').write_text('filesystem full\n')
        assert not comparable(result, summarize(root, 0, 10))
    print('PASS: cumulative snapshots, duplicates, empty tail, partial lines, weighted FPS, windows and comparison gate')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', nargs='*', type=Path, help='candidate then optional baseline')
    parser.add_argument('--start', type=float, default=0, help='seconds from first logged frame window')
    parser.add_argument('--end', type=float, default=float('inf'))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not 1 <= len(args.runs) <= 2 or not 0 <= args.start < args.end:
        parser.error('supply one or two run directories and 0 <= start < end')
    if any(not p.is_dir() for p in args.runs):
        parser.error('run directory does not exist')
    summaries = [summarize(p, args.start, args.end) for p in args.runs]
    for summary in summaries:
        print_summary(summary)
    if len(summaries) == 2:
        a, b = summaries
        if comparable(a, b):
            print(f"Weighted FPS change: {(a['fps'] / b['fps'] - 1) * 100:+.1f}% (matched metadata; verify scene alignment)")
        else:
            print('Speedup withheld: missing/mismatched workload metadata, unverified rendering, missing samples or resource errors.')
    print('FPS values summarize multi-second windows, not per-frame percentiles. Counters/CPU cover the available log, not the selected window.')
    print('Shader-cache acquisition includes cache/locking/compilation; display waits are host wall time, not pure GPU execution. Timing totals cover available receipts only.')
    print('Error reports may be capped; absence of reports is not proof of correctness. No automatic 30 FPS acceptance.')


if __name__ == '__main__':
    main()
