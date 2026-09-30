#!/usr/bin/env python3
"""Reject varying push-constant array indexing in the compiled HUD shader."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / 'hud.spv'
    subprocess.run(['glslangValidator', '-V', '--target-env', 'vulkan1.0',
                    '-o', str(binary), str(root/'headless/vulkan_hud.frag')], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.0', str(binary)], check=True)
    asm = subprocess.check_output(['spirv-dis', str(binary)], text=True)
    constants = set(re.findall(r'(%\w+) = OpConstant\b', asm))
    blocks = set(re.findall(r'(%\w+) = OpVariable %\w+ PushConstant', asm))
    accesses = re.findall(r'OpAccessChain %\w+ (%\w+) ([^\n]+)', asm)
    checked = 0
    for base, indices in accesses:
        if base in blocks:
            assert set(indices.split()) <= constants, 'Varying push-constant index: ' + indices
            checked += 1
    assert checked >= 24, 'Expected all twenty-four live glyph words'
print('HUD SPIR-V: push-constant accesses use constant indices PASS')
