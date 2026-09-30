#!/usr/bin/env python3
"""Check the native axis predicate against saved three-session control logs."""
from pathlib import Path
import sys
from check import guest_devices

host, native = (Path(p).read_text() for p in sys.argv[1:])
guest_devices(host, 3, native=True, axes=True)
guest_devices(native, 3, native=True)
bad_logs = [native]
for field, expected, wrong in (
    ('LX', '0000000000007fff', '0000000000000000'),
    ('RY', '0000000000007fff', '0000000000000000'),
    ('BUTTONS', '0000000000240101', '0000000000240001'),
):
    marker = f'EDEN_GUEST_HID_{field}={expected}'
    assert host.count(marker) == 3
    bad_logs.append(host.replace(marker, f'EDEN_GUEST_HID_{field}={wrong}'))
for log in bad_logs:
    try:
        guest_devices(log, 3, native=True, axes=True)
    except AssertionError:
        continue
    raise AssertionError('Accepted missing trigger or stick coverage')
print('Native axis predicate accepts full controls and rejects Circle-only/missing controls PASS')
