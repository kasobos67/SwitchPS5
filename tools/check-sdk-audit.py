"""Check the bounded SDK probe on host; host results do not qualify the PS5 SDK."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='eden-sdk-audit-') as directory:
    source = Path(directory) / 'check.cpp'
    source.write_text('#include "sdk_audit.h"\nint main() { Eden::AuditSdk(); }\n')
    binary = Path(directory) / 'check'
    subprocess.run(['c++', '-std=c++20', '-pthread', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'headless'), str(source), '-o', str(binary)], check=True)
    output = subprocess.check_output([str(binary)], text=True)
    assert output.count('EDEN_SDK_PARSE') == 4 and output.count('EDEN_SDK_THREAD') == 2
    assert 'pass=0' not in output and 'stack_rc=0' in output
    assert 'supported=1 pass=1' in output
    print(output, end='')
print('SDK audit host harness PASS; console results required separately')
