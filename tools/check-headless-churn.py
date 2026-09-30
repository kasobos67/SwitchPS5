#!/usr/bin/env python3
"""Target TLS/page-table cleanup with process exit during guest thread churn."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'headless'))
from check import receipt, game_log_is_clean

case = Path(tempfile.mkdtemp(prefix='guest-churn-', dir=root / 'results'))
print(case, flush=True)
binary = case / 'eden-headless'
guest = case / 'core-churn.nro'
shutil.copy2(root / 'build/headless-host/eden-headless', binary)
shutil.copy2(root / 'build/fixture/core-churn.nro', guest)
shutil.copy2(root / 'headless/shutdown.gdb', case / 'capture.gdb')
reports = []
for debugger in (True, False):
    directory = case / ('debugger' if debugger else 'plain')
    (directory / 'user').mkdir(parents=True)
    invocation = [str(binary), str(guest), '--soak']
    if debugger:
        invocation = ['gdb', '-q', '-nx', '-batch', '-x', str(case / 'capture.gdb'), '--args', *invocation]
    with (directory / 'process.txt').open('w') as out:
        result = subprocess.run(['timeout', '--signal=INT', '--kill-after=10s', '120s', *invocation],
                                cwd=directory, stdout=out, stderr=subprocess.STDOUT)
    output = (directory / 'process.txt').read_text(errors='replace')
    lines = re.findall(r'^\w+\t(?:PASS|FAIL)$', output, re.M)
    log_path = directory / 'user/log/eden_log.txt'
    log = log_path.read_text(errors='replace') if log_path.exists() else ''
    progress = [int(v, 16) for v in re.findall(r'EDEN_GUEST_CHURN_PROGRESS=([0-9a-f]{16})', log)]
    passed = result.returncode == 0 and game_log_is_clean(log) and len(progress) == 80 and min(progress, default=0) >= 8
    try:
        receipt('\n'.join(lines), log, 20)
        assert log.count('EDEN_GUEST_CHURN_ACTIVE_EXIT') == 20
    except AssertionError:
        passed = False
    reports.append(dict(debugger=debugger, exit_code=result.returncode, passed=passed,
                        progress=progress, completed_shutdowns=lines.count('core_shutdown\tPASS')))
    (case / 'assessment.json').write_text(json.dumps(dict(
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        fixture_sha256=hashlib.sha256(guest.read_bytes()).hexdigest(), runs=reports,
        scope='Targeted active-thread/TLS cleanup diagnostic; historical crash is not fixed by clean runs.'
    ), indent=2) + '\n')
    print(directory.name, 'PASS' if passed else 'FAILED; stopped for stack inspection', flush=True)
    if not passed:
        raise SystemExit(1)
