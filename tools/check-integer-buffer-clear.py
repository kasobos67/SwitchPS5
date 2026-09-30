#!/usr/bin/env python3
"""Check integer buffer-clear source formats, including the original regression."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
cache = Path((root / '.local/headless-cache').read_text().strip())

def check(source):
    calls = re.findall(r'glClear(?:Named)?Buffer(?:Sub)?Data\s*\([^;]+;', source)
    for call in calls:
        formats = re.findall(r'\bGL_[A-Z0-9_]+\b', call)
        internal = [f for f in formats if re.fullmatch(r'GL_R(?:G(?:B(?:A)?)?)?(?:8|16|32)UI?', f)]
        if internal:
            assert any(f in formats for f in ('GL_RED_INTEGER', 'GL_RG_INTEGER',
                       'GL_RGB_INTEGER', 'GL_RGBA_INTEGER')), call
    return len(calls)

original = (cache / 'source/src/video_core/renderer_opengl/gl_buffer_cache.cpp').read_text()
fixed = (cache / 'native-local/headless/gl_buffer_cache.cpp').read_text()
assert fixed == original.replace('static_cast<GLsizeiptr>(size), GL_RED, GL_UNSIGNED_INT, &value);',
                                'static_cast<GLsizeiptr>(size), GL_RED_INTEGER, GL_UNSIGNED_INT, &value);')
assert check(fixed) == 1
for bad in (original, fixed.replace('GL_RED_INTEGER', 'GL_RED')):
    try:
        check(bad)
    except AssertionError:
        pass
    else:
        raise AssertionError('Accepted integer clear with non-integer source format')
for path in (cache / 'source/src/video_core').rglob('*.cpp'):
    if path.name != 'gl_buffer_cache.cpp':
        check(path.read_text())
print('Integer buffer clear format, sibling call sites and rejection controls PASS')
