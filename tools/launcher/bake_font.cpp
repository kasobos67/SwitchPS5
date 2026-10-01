// ProsperoEden - Offline SDF font atlas baker (host tool).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: bake_font <font.ttf> <out.pefont> [pixel_size=56] [sdf_range=8] [atlas_width=2048]
// Bakes Latin (with accents), Cyrillic and a few symbols into a single-channel
// signed distance field atlas with metrics and kerning (see pe/gfx/font_format.hpp).

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include "pe/gfx/font_format.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{

namespace ff = pe::gfx::font_format;

struct Baked
{
    int codepoint = 0;
    int glyph = 0;
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    float advance = 0.0f;
    unsigned char *bitmap = nullptr;
    int x = 0;
    int y = 0;
};

std::vector<int> codepoints()
{
    std::vector<int> result;
    const int ranges[][2] = {{0x20, 0x7e},     // Basic Latin
                             {0xa0, 0xff},     // Latin-1 Supplement
                             {0x100, 0x17f},   // Latin Extended-A
                             {0x1a0, 0x1b0},   // Vietnamese horn letters
                             {0x218, 0x21b},   // Romanian comma letters
                             {0x400, 0x45f},   // Cyrillic
                             {0x490, 0x491},   // Ukrainian ghe with upturn
                             {0x1ea0, 0x1ef9}, // Vietnamese
                             {0x2190, 0x2193}}; // arrows
    for (const auto &range : ranges)
        for (int c = range[0]; c <= range[1]; ++c)
            result.push_back(c);
    // Dashes, quotes, bullet, ellipsis, angle quotes, euro, numero, trade mark.
    const int extra[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201a, 0x201c, 0x201d, 0x201e,
                         0x2022, 0x2026, 0x2039, 0x203a, 0x20ac, 0x2116, 0x2122};
    result.insert(result.end(), std::begin(extra), std::end(extra));
    return result;
}

template <typename T> void put(std::vector<unsigned char> &out, const T &value)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s font.ttf out.pefont [pixel_size] [sdf_range] [atlas_width]\n",
                     argv[0]);
        return 2;
    }
    const float pixel_size = argc > 3 ? std::strtof(argv[3], nullptr) : 56.0f;
    const int range = argc > 4 ? std::atoi(argv[4]) : 8;
    const int atlas_width = argc > 5 ? std::atoi(argv[5]) : 2048;
    constexpr int kMaxHeight = 8192;

    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
    stbtt_fontinfo font;
    if (ttf.empty() || !stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
    {
        std::fprintf(stderr, "cannot read font %s\n", argv[1]);
        return 1;
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixel_size);

    std::vector<Baked> glyphs;
    int missing = 0;
    for (int codepoint : codepoints())
    {
        const int index = stbtt_FindGlyphIndex(&font, codepoint);
        if (codepoint != ' ' && index == 0)
        {
            ++missing;
            continue;
        }
        Baked glyph;
        glyph.codepoint = codepoint;
        glyph.glyph = index;
        int advance = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&font, index, &advance, &bearing);
        glyph.advance = static_cast<float>(advance) * scale;
        glyph.bitmap = stbtt_GetGlyphSDF(&font, scale, index, range, 128,
                                         128.0f / static_cast<float>(range), &glyph.w, &glyph.h,
                                         &glyph.xoff, &glyph.yoff);
        glyphs.push_back(glyph);
    }

    // Shelf packing, tallest first, one pixel of spacing.
    std::vector<Baked *> order;
    for (Baked &glyph : glyphs)
        order.push_back(&glyph);
    std::sort(order.begin(), order.end(), [](const Baked *a, const Baked *b) { return a->h > b->h; });
    int pen_x = 1;
    int pen_y = 1;
    int shelf = 0;
    for (Baked *glyph : order)
    {
        if (glyph->bitmap == nullptr)
            continue;
        if (pen_x + glyph->w + 1 > atlas_width)
        {
            pen_x = 1;
            pen_y += shelf + 1;
            shelf = 0;
        }
        if (pen_y + glyph->h + 1 > kMaxHeight)
        {
            std::fprintf(stderr, "the atlas would be taller than %d\n", kMaxHeight);
            return 1;
        }
        glyph->x = pen_x;
        glyph->y = pen_y;
        pen_x += glyph->w + 1;
        shelf = std::max(shelf, glyph->h);
    }
    const int atlas_height = pen_y + shelf + 1;
    std::vector<unsigned char> atlas(static_cast<std::size_t>(atlas_width) * atlas_height, 0);
    for (const Baked &glyph : glyphs)
    {
        for (int row = 0; glyph.bitmap != nullptr && row < glyph.h; ++row)
            std::memcpy(&atlas[static_cast<std::size_t>(glyph.y + row) * atlas_width + glyph.x],
                        glyph.bitmap + row * glyph.w, static_cast<std::size_t>(glyph.w));
    }

    std::vector<ff::Kern> kerns;
    for (const Baked &a : glyphs)
    {
        for (const Baked &b : glyphs)
        {
            const int kern = stbtt_GetGlyphKernAdvance(&font, a.glyph, b.glyph);
            if (kern != 0)
                kerns.push_back(ff::Kern{static_cast<std::uint32_t>(a.codepoint),
                                         static_cast<std::uint32_t>(b.codepoint),
                                         static_cast<float>(kern) * scale});
        }
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    ff::Header header{};
    header.magic = ff::kMagic;
    header.version = ff::kVersion;
    header.atlas_width = static_cast<std::uint16_t>(atlas_width);
    header.atlas_height = static_cast<std::uint16_t>(atlas_height);
    header.pixel_size = pixel_size;
    header.sdf_range = static_cast<float>(range);
    header.ascent = static_cast<float>(ascent) * scale;
    header.descent = static_cast<float>(descent) * scale;
    header.line_gap = static_cast<float>(line_gap) * scale;
    header.glyph_count = static_cast<std::uint32_t>(glyphs.size());
    header.kern_count = static_cast<std::uint32_t>(kerns.size());

    std::vector<unsigned char> out;
    put(out, header);
    std::sort(glyphs.begin(), glyphs.end(),
              [](const Baked &a, const Baked &b) { return a.codepoint < b.codepoint; });
    for (const Baked &glyph : glyphs)
    {
        ff::Glyph record{};
        record.codepoint = static_cast<std::uint32_t>(glyph.codepoint);
        record.x = static_cast<std::uint16_t>(glyph.x);
        record.y = static_cast<std::uint16_t>(glyph.y);
        record.w = static_cast<std::uint16_t>(glyph.w);
        record.h = static_cast<std::uint16_t>(glyph.h);
        record.offset_x = static_cast<float>(glyph.xoff);
        record.offset_y = static_cast<float>(glyph.yoff);
        record.advance = glyph.advance;
        put(out, record);
    }
    std::sort(kerns.begin(), kerns.end(), [](const ff::Kern &a, const ff::Kern &b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    for (const ff::Kern &kern : kerns)
        put(out, kern);
    out.insert(out.end(), atlas.begin(), atlas.end());

    std::FILE *file = std::fopen(argv[2], "wb");
    if (file == nullptr || std::fwrite(out.data(), 1, out.size(), file) != out.size())
    {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::fclose(file);
    for (Baked &glyph : glyphs)
        stbtt_FreeSDF(glyph.bitmap, nullptr);
    std::printf("%s: %zu glyphs (%d not in the font), %zu kerning pairs, atlas %dx%d, %zu bytes\n",
                argv[2], glyphs.size(), missing, kerns.size(), atlas_width, atlas_height, out.size());
    return 0;
}
