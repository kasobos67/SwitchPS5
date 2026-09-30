"""Verify the ASTC adaptation preserves every GPU command and barrier.

This is a source invariant check, not hardware synchronization validation.
Arguments: pinned Eden source directory, generated port directory.
"""
from pathlib import Path
import sys

source, generated = map(Path, sys.argv[1:])
original = (source / 'src/video_core/renderer_vulkan/vk_compute_pass.cpp').read_text()
adapted = (generated / 'vk_compute_pass_cost.cpp').read_text()

def body(text):
    return text.split('void ASTCDecoderPass::Assemble(', 1)[1].split(
        'constexpr u32 BL3D_BINDING_INPUT_BUFFER', 1)[0]

before, after = body(original), body(adapted)
assert before.count('scheduler.Finish();') == 1
assert after == before.replace('scheduler.Finish();',
    '{ auto timer = Eden::Performance::VulkanTimer(12); scheduler.Flush(); }')
assert 'VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT' in after
assert 'cmdbuf.Dispatch(' in after and 'scheduler.Finish()' not in after
print('ASTC GPU commands/barriers preserved; only completion wait changed to submission PASS')
