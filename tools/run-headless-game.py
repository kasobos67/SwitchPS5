#!/usr/bin/env python3
"""Linux-only, bounded Null-renderer diagnostic; no gameplay qualification."""
import argparse
import collections
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'headless'))
from check import game_log_is_clean, game_lifecycle_is_complete, filesystem_call_counts, game_session_markers_match

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('game', type=Path)
parser.add_argument('keys', type=Path, help='Directory containing prod.keys')
parser.add_argument('firmware', type=Path, help='Directory containing firmware NCA files')
parser.add_argument('--binary', type=Path, default=root / 'build/headless-host/eden-headless',
                    help='Host binary to test (for example an ASan build)')
parser.add_argument('--repeat', action='store_true', help='Load and stop three times in one process')
args = parser.parse_args()
if not args.binary.is_file():
    parser.error('Host binary is missing')
if not args.game.is_file():
    parser.error('Game file is missing')
if not (args.keys / 'prod.keys').is_file():
    parser.error('prod.keys is missing')
firmware = list(args.firmware.glob('*.nca'))
if not firmware:
    parser.error('No firmware NCA files found')
(root / 'results').mkdir(exist_ok=True)
run = Path(tempfile.mkdtemp(prefix='game-host-' + datetime.datetime.now(
    datetime.timezone.utc).strftime('%Y%m%d-%H%M%S') + '-', dir=root / 'results'))
print(run, flush=True)
shutil.copy2(args.binary, run / 'eden-headless')
keys = run / 'user/keys'
keys.mkdir(parents=True)
for name in ('prod.keys', 'title.keys'):
    if (args.keys / name).is_file():
        shutil.copyfile(args.keys / name, keys / name)
        (keys / name).chmod(0o600)
registered = run / 'user/nand/system/Contents/registered'
registered.mkdir(parents=True)
for source in firmware:
    shutil.copyfile(source, registered / source.name)
cycles = 3 if args.repeat else 1
with (run / 'result.tsv').open('wb') as out, (run / 'stderr.log').open('wb') as err:
    result = subprocess.run(['/usr/bin/time', '-v', '-o', str(run / 'time.txt'),
        'timeout', '--kill-after=5s', f'{90 * cycles}s', './eden-headless', str(args.game.resolve()),
        '--game', *(['--repeat'] if args.repeat else [])], cwd=run, stdout=out, stderr=err)
receipt = (run / 'result.tsv').read_text().splitlines()
lifecycle_ok = result.returncode == 0 and game_lifecycle_is_complete(receipt, cycles)
log_path = run / 'user/log/eden_log.txt'
log = log_path.read_text(errors='replace') if log_path.exists() else ''
sessions = re.findall(r'EDEN_GAME_SESSION_BEGIN (\d+)(.*?)EDEN_GAME_SESSION_END \1\b', log, re.S)
session_sequence_ok = game_session_markers_match(log, cycles)
diagnostic_ok = lifecycle_ok and game_log_is_clean(log) and session_sequence_ok
stderr = (run / 'stderr.log').read_text(errors='replace')
peak_rss = re.search(r'Maximum resident set size \(kbytes\): (\d+)', (run / 'time.txt').read_text())
storage_files = []
for relative in ('nand/user/save', 'nand/system/save', 'sdmc'):
    for path in sorted((run / 'user' / relative).rglob('*')):
        if path.is_file():
            with path.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            storage_files.append({'path': str(path.relative_to(run / 'user')),
                                  'bytes': path.stat().st_size, 'sha256': digest})
report = {
    'game': args.game.name, 'platform': 'Linux host', 'renderer': 'Null',
    'binary_sha256': hashlib.sha256((run / 'eden-headless').read_bytes()).hexdigest(),
    'exit_code': result.returncode, 'lifecycle_ok': lifecycle_ok, 'diagnostic_ok': diagnostic_ok,
    'session_sequence_ok': session_sequence_ok,
    'observation_seconds_per_cycle': 30, 'requested_cycles': cycles,
    'guest_early_exits': receipt.count('guest_exit_callback\tPASS'),
    'session_begins': re.findall(r'EDEN_GAME_SESSION_BEGIN (\d+)', log),
    'session_ends': re.findall(r'EDEN_GAME_SESSION_END (\d+)', log),
    'filesystem_call_counts': filesystem_call_counts(log),
    'filesystem_calls_per_cycle': {number: filesystem_call_counts(section) for number, section in sessions},
    'storage_files_after_run': storage_files,
    'service_log_counts': dict(collections.Counter(re.findall(r'\] (Service\.[\w.]+) ', log))),
    'critical_log_lines': len(re.findall(r'<Critical>', log)),
    'peak_rss_kib': int(peak_rss[1]) if peak_rss else None,
    'heap_samples': [dict(re.findall(r'(\w+)=(\w+)', line)) for line in stderr.splitlines()
                     if line.startswith('host_heap ')],
    'qualification': 'Host lifecycle only; service logs do not establish sustained game progress.',
}
(run / 'assessment.json').write_text(json.dumps(report, indent=2) + '\n')
if not diagnostic_ok:
    raise SystemExit(f'Incomplete lifecycle/session log or critical assertion; inspect {run}')
print('HOST_GAME_LIFECYCLE_PASS (gameplay unqualified)', flush=True)
