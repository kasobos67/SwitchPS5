#!/usr/bin/env python3
"""Merge baked launcher fonts, preferring Nintendo and retaining missing glyphs.

The launcher binary format and font renderer stay unchanged. Run after baking
both TTF files with bake_font.cpp, using the same pixel size and SDF range.
"""
import argparse
import struct
from pathlib import Path

HEADER = struct.Struct('<IIHH5fII')
GLYPH = struct.Struct('<IHHHHfff')
KERN = struct.Struct('<IIf')


def load(path):
    data = Path(path).read_bytes()
    header = HEADER.unpack_from(data)
    assert header[:2] == (0x46505a50, 1), 'Unsupported baked font'
    glyph_count, kern_count = header[-2:]
    glyphs = [GLYPH.unpack_from(data, HEADER.size + i * GLYPH.size)
              for i in range(glyph_count)]
    start = HEADER.size + glyph_count * GLYPH.size
    kern = [KERN.unpack_from(data, start + i * KERN.size) for i in range(kern_count)]
    atlas = data[start + kern_count * KERN.size:]
    width, height = header[2:4]
    assert len(atlas) == width * height
    assert len({g[0] for g in glyphs}) == glyph_count
    for _, x, y, w, h, *_ in glyphs:
        assert x + w <= width and y + h <= height
    return header, {g[0]: g for g in glyphs}, kern, atlas


def merge(primary_path, fallback_path, output_path):
    primary = load(primary_path)
    fallback = load(fallback_path)
    assert primary[0][4:6] == fallback[0][4:6], 'Bake with identical em and SDF spread'
    missing = fallback[1].keys() - primary[1].keys()
    width = primary[0][2]
    rows = bytearray(width * 4096)
    x, y, row_height = 2, 2, 0
    glyphs = []
    for cp in sorted(primary[1].keys() | fallback[1].keys()):
        font = primary if cp in primary[1] else fallback
        _, sx, sy, w, h, ox, oy, advance = font[1][cp]
        assert w + 4 <= width
        if x + w + 2 > width:
            x, y, row_height = 2, y + row_height + 2, 0
        assert y + h + 2 < 4096, 'Atlas exceeds the existing renderer limit'
        source_width = font[0][2]
        for r in range(h):
            rows[(y+r)*width+x:(y+r)*width+x+w] = font[3][(sy+r)*source_width+sx:(sy+r)*source_width+sx+w]
        glyphs.append((cp, x, y, w, h, ox, oy, advance))
        x += w + 2
        row_height = max(row_height, h)
    height = y + row_height + 2
    # Cross-font kerning is undefined. Preserve only pairs belonging to one font.
    kern = [k for k in primary[2] if k[0] in primary[1] and k[1] in primary[1]]
    kern += [k for k in fallback[2] if k[0] in missing and k[1] in missing]
    kern.sort(key=lambda k: (k[0], k[1]))
    header = list(primary[0]); header[3] = height; header[-2:] = [len(glyphs), len(kern)]
    result = HEADER.pack(*header)
    result += b''.join(GLYPH.pack(*g) for g in glyphs)
    result += b''.join(KERN.pack(*k) for k in kern)
    result += rows[:width*height]
    Path(output_path).write_bytes(result)
    verified = load(output_path)
    assert verified[1].keys() >= fallback[1].keys() | primary[1].keys()
    print(f'Nintendo glyphs: {len(primary[1])}; fallback: {len(missing)}; '
          f'total: {len(glyphs)}; atlas: {width}x{height}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('primary')
    parser.add_argument('fallback')
    parser.add_argument('output')
    args = parser.parse_args()
    merge(args.primary, args.fallback, args.output)
