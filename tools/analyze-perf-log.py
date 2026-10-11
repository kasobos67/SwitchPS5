#!/usr/bin/env python3
"""Say where a development run's frame time went.

The GPU thread prints one report every five seconds (EDEN_VULKAN_FRAME, then
EDEN_DEV_GPU and the lines under it). Those are cumulative since the process
started, so every rate below is a difference between consecutive reports - the
first report has nothing to difference against and is left out.

    tools/analyze-perf-log.py heap.log            one run
    tools/analyze-perf-log.py --windows heap.log  the per-window table too
    tools/analyze-perf-log.py a.log b.log         two runs side by side
"""
import math
import re
import sys
from pathlib import Path

TOKEN = re.compile(r'\b(EDEN_[A-Z0-9_]+)')
FIELD = re.compile(r'([A-Za-z_]\w*)=([^\s]+)')

# Cumulative counters (ReportGpuThread's "Cumulative totals; the analyzer takes
# deltas between consecutive reports"). Everything else printed inside a window
# is already a window's worth or a point-in-time snapshot.
CUMULATIVE = {
    'EDEN_DEV_GPU': 'gpu',
    'EDEN_DEV_DRAW': 'draw',
    'EDEN_DEV_PUSH': 'push',
    'EDEN_DEV_RAST': 'rast',
    'EDEN_DEV_GC_DRAIN': 'gc',
    'EDEN_DEV_GUEST': 'guest',
}
SNAPSHOT = {'EDEN_PERF_DIRECT': 'direct'}

# The six phases of RasterizerVulkan::PrepareDraw, in the order they run.
PHASES = ('flush', 'pipeline', 'lock_wait', 'config', 'state', 'record')


def number(value):
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        return None


def parse(path):
    events = []
    for line in path.read_text(errors='replace').splitlines():
        match = TOKEN.search(line)
        if not match:
            continue
        values = {key: number(raw) if number(raw) is not None else raw
                  for key, raw in FIELD.findall(line[match.start():])}
        if values:
            events.append((match.group(1), values))
    return events


def split_windows(events):
    """Every event up to the next EDEN_VULKAN_FRAME belongs to the window it opens."""
    windows, current = [], None
    for kind, values in events:
        if kind == 'EDEN_VULKAN_FRAME':
            if current is not None:
                windows.append(current)
            current = {'frame': values, 'groups': {}}
        elif current is not None:
            current['groups'].setdefault(kind, []).append(values)
    if current is not None:
        windows.append(current)
    return windows


def build_rows(windows):
    rows, previous = [], {}
    for index, window in enumerate(windows):
        row = dict(window['frame'])
        row['index'] = index
        for kind, values in window['groups'].items():
            if kind not in CUMULATIVE:
                continue
            raw = values[0]
            base = previous.get(kind)
            if base is not None:
                for key, value in raw.items():
                    before = base.get(key)
                    if not isinstance(value, (int, float)) or not isinstance(before, (int, float)):
                        continue
                    step = value - before
                    # A decrease is the counters starting over, not a negative interval.
                    row[f'{CUMULATIVE[kind]}.{key}'] = step if step >= 0 else value
            previous[kind] = raw
        for kind, values in window['groups'].items():
            if kind in SNAPSHOT:
                for key, value in values[-1].items():
                    row[f'{SNAPSHOT[kind]}.{key}'] = value
        derive(row)
        rows.append(row)
    return rows


def derive(row):
    """Rates a report window can stand on its own. Window 0 has no deltas, so it gets none."""
    gpu_frame = row.get('gpu.frame')
    draws = row.get('draw.calls')
    dispatch = row.get('gpu.dispatch_ns')
    rast = row.get('draw.ns')
    if gpu_frame and draws and dispatch and rast:
        row['dispatch_ms'] = dispatch / 1e6
        row['rast_ms'] = rast / 1e6
        row['disp_us_per_frame'] = dispatch / gpu_frame / 1000.0
        row['rast_us_per_draw'] = rast / draws / 1000.0
        row['decode_us_per_draw'] = (dispatch - rast) / draws / 1000.0
        row['draws_per_frame'] = draws / gpu_frame
        row['present_us_per_frame'] = row.get('gpu.present_ns', 0) / gpu_frame / 1000.0
        for phase in PHASES:
            spent = row.get(f'rast.{phase}_ns')
            if spent is not None:
                row[f'phase_{phase}_us'] = spent / draws / 1000.0
        unattributed = row.get('rast.unattributed_ns')
        if unattributed is not None and rast:
            row['unattributed_frac'] = unattributed / rast
    if gpu_frame:
        for key, name in (('guest.ipc_ns', 'ipc_ms'),
                          ('guest.cache_lock_contended', 'lockcont_per_frame'),
                          ('guest.cache_lock_blocked', 'lockblocked_per_frame'),
                          ('guest.cpu_read_ns', 'cpu_read_ms')):
            if row.get(key) is not None:
                row[name] = row[key] / gpu_frame / (1e6 if name.endswith('_ms') else 1)
    return row


def percentile(values, portion):
    ordered = sorted(values)
    if not ordered:
        return float('nan')
    position = (len(ordered) - 1) * portion
    low, high = math.floor(position), math.ceil(position)
    if low == high:
        return ordered[int(position)]
    return ordered[low] * (high - position) + ordered[high] * (position - low)


def summarize(values):
    return {'min': min(values), 'p25': percentile(values, 0.25),
            'med': percentile(values, 0.5), 'p75': percentile(values, 0.75),
            'max': max(values), 'mean': sum(values) / len(values)}


def pearson(xs, ys):
    if len(xs) < 3:
        return float('nan')
    mean_x, mean_y = sum(xs) / len(xs), sum(ys) / len(ys)
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    sxx = sum((x - mean_x) ** 2 for x in xs)
    syy = sum((y - mean_y) ** 2 for y in ys)
    if sxx <= 0 or syy <= 0:
        return float('nan')
    return sxy / math.sqrt(sxx * syy)


# Columns worth printing for one run: fps first, then the dispatch both ways round -
# the wall time it took (dispatch_ms, rast_ms) and what it costs per unit of work
# (us per frame, us per draw), since only the first of those moves with fps.
CORE = ('fps', 'dispatch_ms', 'rast_ms', 'disp_us_per_frame', 'rast_us_per_draw',
        'decode_us_per_draw', 'draws_per_frame', 'lockcont_per_frame', 'ipc_ms')


def report(rows, path, show_windows):
    usable = [row for row in rows if 'disp_us_per_frame' in row]
    if not usable:
        print(f'{path}: no report window has a previous one to difference against')
        return None
    frame_rows = [row for row in rows if 'fps' in row]
    frame_rows = [row for row in frame_rows if row['index'] > 0] or frame_rows

    print(f'\n{path}  {len(rows)} reports, {len(usable)} usable, '
          f"{sum(r.get('frames', 0) for r in frame_rows)} frames "
          f"{sum(r.get('seconds', 0) for r in frame_rows):.1f}s")
    fps = [row['fps'] for row in frame_rows if row.get('fps')]
    if fps:
        stats = summarize(fps)
        under = sum(1 for value in fps if value < 30) / len(fps) * 100
        print(f"  fps   min {stats['min']:.1f}  p25 {stats['p25']:.1f}  med {stats['med']:.1f}  "
              f"p75 {stats['p75']:.1f}  max {stats['max']:.1f}  mean {stats['mean']:.1f}  "
              f"{under:.0f}% of windows under 30")

    present = [name for name in CORE[1:] if any(name in row for row in usable)]
    print('  ' + f'{"metric":>22} ' + '  '.join(f'{key:>6}' for key in
                                                ('min', 'p25', 'med', 'p75', 'max', 'mean', 'r')))
    for name in present:
        values = [row[name] for row in usable if name in row]
        pairs = [(row['fps'], row[name]) for row in usable if 'fps' in row and name in row]
        if not values or not pairs:
            continue
        stats = summarize(values)
        cells = '  '.join(f'{stats[key]:>6.2f}' for key in ('min', 'p25', 'med', 'p75', 'max', 'mean'))
        print('  ' + f'{name:>22} ' + cells + f'  {pearson(*zip(*pairs)):>6.2f}')

    report_phases(usable)
    if show_windows:
        print('\n    win     fps  disp/f  rast/f  us/draw  draws/f  ipc_ms  lockcont')
        for row in usable:
            if 'fps' not in row:
                continue
            print(f"  {row['index']:>4} {row['fps']:>7.1f} {row['disp_us_per_frame']:>7.0f} "
                  f"{row['rast_ms']:>7.1f} {row['rast_us_per_draw']:>8.2f} "
                  f"{row['draws_per_frame']:>8.1f} {row.get('ipc_ms', float('nan')):>7.1f} "
                  f"{row.get('lockcont_per_frame', 0):>9.0f}")
    summary = {'fps': summarize(fps) if fps else {}}
    for name in present:
        values = [row[name] for row in usable if name in row]
        if values:
            summary[name] = summarize(values)
    for phase in PHASES:
        values = [row[f'phase_{phase}_us'] for row in usable if f'phase_{phase}_us' in row]
        if values:
            summary[f'phase.{phase}'] = summarize(values)
    return summary


def report_phases(rows):
    """The rasterizer's time, shared out over the six phases of PrepareDraw."""
    if not any(f'rast.{phase}_ns' in row for row in rows for phase in PHASES):
        return  # no EDEN_DEV_RAST in this log - it predates the six-phase split
    spent = {phase: sum(row.get(f'rast.{phase}_ns', 0) for row in rows) for phase in PHASES}
    unattributed = sum(row.get('rast.unattributed_ns', 0) for row in rows)
    total = sum(row.get('draw.ns', 0) for row in rows)
    draws = sum(row.get('draw.calls', 0) for row in rows)
    if not total or not draws:
        return None
    print(f'\n  PrepareDraw, {total / 1e6:.1f} s over {draws:,} draws '
          f'({total / draws / 1000:.2f} us/draw), by phase:')
    for phase in PHASES:
        share = spent[phase] / total * 100
        print(f'    {phase:>10} {spent[phase] / 1e6:>8.1f} s  {share:>5.1f}%  '
              f'{spent[phase] / draws / 1000:>7.3f} us')
    print(f'    {"unattributed":>10} {unattributed / 1e6:>8.1f} s  {unattributed / total * 100:>5.1f}%'
          f'  {unattributed / draws / 1000:>7.3f} us   <- early returns the Timer still charged')


def compare(runs):
    names = list(runs)
    print('\n  side by side:')
    print('  ' + f'{"":>24}' + '  '.join(f'{name[-24:]:>24}' for name in names))
    keys = sorted({key for summary in runs.values() for key, value in summary.items()
                   if isinstance(value, dict) and value})
    for key in keys:
        for statistic in ('mean', 'p25', 'med', 'p75'):
            cells = []
            for name in names:
                value = runs[name].get(key, {}).get(statistic)
                cells.append('-' if value is None else f'{value:.2f}')
            label = key if statistic == 'mean' else f'  {statistic}'
            print(f'  {label:>24} ' + '  '.join(f'{cell:>24}' for cell in cells))


def main(argv):
    paths, show_windows = [], False
    for argument in argv:
        if argument == '--windows':
            show_windows = True
        else:
            paths.append(argument)
    if not paths:
        paths = ['heap.log']
    runs = {}
    for path in paths:
        file = Path(path)
        if not file.is_file():
            print(f'{path}: no such file', file=sys.stderr)
            return 1
        summary = report(build_rows(split_windows(parse(file))), path, show_windows)
        if summary is not None:
            runs[path] = summary
    if len(runs) > 1:
        compare(runs)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
