#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A development run's report has to arrive, and it has to add up.

    tools/check-perf-log.py [heap.log]

Reads the log with tools/analyze-perf-log.py and asserts what every report window
must satisfy. This is a check on a captured run rather than on source, so it is
run by hand after a session:

  the report fired           two EDEN_VULKAN_FRAME windows, the second with the
                             counters under it to difference against
  every line the analyzer
  needs still prints         EDEN_DEV_GPU, _DRAW, _PUSH, _RAST and _GUEST
  the nesting holds          a draw is charged inside the dispatch it ran in, and
                             the dispatch's own phases do not exceed the dispatch
  the six phases fit         they plus what the early returns left are the whole
                             of EDEN_DEV_DRAW, no more and no less
  they run in order          flush reaches every draw that got a pipeline, and no
                             later phase is marked more often than the one before
  config splits              when EDEN_DEV_CFG is there at all, its six phases are
                             all there and fit inside config, in Configure's order
  bind splits                when EDEN_DEV_BIND is there at all, its three fit
                             inside cfg_bind, in the same order
  fps is a rate              over zero, and not past a thousand
"""
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('analyze_perf_log', ROOT / 'tools/analyze-perf-log.py')
analyze = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyze)

NEEDED = ('gpu.dispatch_ns', 'gpu.dispatch_calls', 'gpu.frame', 'gpu.cpu_ns', 'gpu.idle_ns',
          'draw.ns', 'draw.calls', 'push.lock_ns', 'push.push_ns', 'push.dispatch_ns',
          'guest.ipc_ns', 'guest.cache_lock_contended')


def main(argv):
    path = Path(argv[1]) if len(argv) > 1 else ROOT / 'heap.log'
    if not path.is_file():
        print(f'{path}: no such file - capture a run first', file=sys.stderr)
        return 1
    windows = analyze.build_rows(analyze.split_windows(analyze.parse(path)))
    usable = [row for row in windows if 'disp_us_per_frame' in row]
    assert len(usable) >= 2, (f'{path.name}: {len(usable)} report window(s) with a previous one '
                              'to difference against, want at least 2 (a 5 s report each)')

    for name in NEEDED + tuple(f'rast.{phase}_ns' for phase in analyze.PHASES):
        assert any(name in row for row in usable), (
            f'{path.name}: {name} is missing - ReportGpuThread stopped printing it')

    for row in usable:
        index = row['index']
        assert row['draw.ns'] <= row['gpu.dispatch_ns'] * 1.05, (
            f'{path.name} window {index}: the rasterizer charged {row["draw.ns"]} ns, more than '
            f'the dispatch it ran inside ({row["gpu.dispatch_ns"]} ns)')
        assert row['push.dispatch_ns'] <= row['gpu.dispatch_ns'] * 1.02, (
            f'{path.name} window {index}: the dma pusher charged {row["push.dispatch_ns"]} ns, '
            f'more than the dispatch that contains it ({row["gpu.dispatch_ns"]} ns)')
        assert 0 < row['fps'] <= 1000, f'{path.name} window {index}: fps {row["fps"]}'

    spent = sum(row.get('draw.ns', 0) for row in usable)
    phases = {name: sum(row.get(f'rast.{name}_ns', 0) for row in usable)
              for name in analyze.PHASES}
    counted = {name: sum(row.get(f'rast.{name}_calls', 0) for row in usable)
               for name in analyze.PHASES}
    assert spent > 0, f'{path.name}: no time in PrepareDraw over {sum(counted.values())} phases'
    assert sum(phases.values()) <= spent * 1.01, (
        f'{path.name}: the six phases come to {sum(phases.values())} ns, more than the '
        f'{spent} ns the Timer charged for the whole draw')
    covered = sum(phases.values()) / spent
    assert covered >= 0.5, (
        f'{path.name}: the six phases account for {covered:.0%} of PrepareDraw - a Mark is not '
        'running')
    assert counted['flush'] > 0, f'{path.name}: the flush phase never ran'
    for earlier, later in zip(analyze.PHASES, analyze.PHASES[1:]):
        assert counted[later] <= counted[earlier], (
            f'{path.name}: {later} was marked {counted[later]} times but {earlier} only '
            f'{counted[earlier]} - a phase is marked that the one before it never reached')

    # Each nested group prints all of its phases or none of them - half a line means a printf
    # was cut - fits inside the parent it nests in, and is marked in the parent's order.
    label = {'cfg': 'config', 'bind': 'cfg_bind', 'geo': 'bind_geometry', 'upd': 'bind_update'}
    counted_words = {3: 'three', 4: 'four', 6: 'six'}
    results = []
    for prefix, phases, _key, total_key, _title, _foot in analyze.GROUPS:
        names = tuple(f'{prefix}.{phase}_ns' for phase in phases)
        present = sum(1 for name in names if any(name in row for row in usable))
        assert present in (0, len(names)), (
            f'{path.name}: {present} of {len(names)} {prefix} phases printed - that line is '
            'missing part of its fields')
        if present != len(names):
            results.append((label[prefix], None))
            continue
        total = sum(row.get(total_key, 0) for row in usable)
        spent = {phase: sum(row.get(f'{prefix}.{phase}_ns', 0) for row in usable)
                 for phase in phases}
        counted = {phase: sum(row.get(f'{prefix}.{phase}_calls', 0) for row in usable)
                   for phase in phases}
        assert sum(spent.values()) <= total * 1.01, (
            f'{path.name}: the {len(phases)} phases inside {label[prefix]} come to '
            f'{sum(spent.values())} ns, more than the {total} ns it was charged')
        share = sum(spent.values()) / total if total else 0
        assert share >= 0.5, (
            f'{path.name}: the {len(phases)} phases account for {share:.0%} of '
            f'{label[prefix]} - a Mark is not running')
        assert counted[phases[0]] > 0, (
            f'{path.name}: the {prefix} {phases[0]} phase never ran')
        for earlier, later in zip(phases, phases[1:]):
            assert counted[later] <= counted[earlier], (
                f'{path.name}: {prefix} {later} was marked {counted[later]} times but {earlier} '
                f'only {counted[earlier]} - a phase is marked that the one before it never '
                'reached')
        results.append((label[prefix], share))

    print(f'perf log {path.name}: {len(usable)} windows, {covered:.0%} of PrepareDraw in the six '
          'phases, report lines and nesting PASS'
          + ''.join(
              f', {share:.0%} of {name} in its {counted_words[len(analyze.GROUPS[i][1])]}'
              if share is not None else f', no {name} split'
              for i, (name, share) in enumerate(results)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
