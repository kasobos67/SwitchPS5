// ProsperoEden - The console's own fonts, for the scripts the launcher's baked font lacks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pe::gfx
{

// The launcher's own font (Montserrat, baked into an atlas) covers Latin and Cyrillic. Japanese,
// Korean, Chinese, Greek, Thai and Arabic come from the font files the PS5 system software
// carries (/preinst/common/font): nothing is shipped with the app. A file is read the first time
// one of its characters is needed.
//
// Shaping (which glyph a character becomes next to its neighbours, and where marks sit) is
// HarfBuzz's; the outlines are turned into distance fields like those of the baked atlas.

// The folders tried for the console's fonts, the best first.
std::vector<std::string> system_font_folders();
// The font files to try, in order, for text shown in `language` (a PS5 language tag such as
// "ja-JP", "zh-Hans", "ar"): that language's own font first, so that characters shared between
// Japanese, Chinese and Korean get its forms.
std::vector<std::string> system_font_files(std::string_view folder, std::string_view language);

// One glyph of a shaped run: positions in em, x to the right and y up from the baseline.
struct RunGlyph
{
    std::uint32_t id = 0; // glyph index in the face
    float x_offset = 0.0f;
    float y_offset = 0.0f;
    float advance = 0.0f;
};

// A glyph as a distance field: 128 on the outline, falling by 128 / range per pixel outward.
struct GlyphField
{
    int w = 0; // 0: the glyph draws nothing (a space)
    int h = 0;
    float offset_x = 0.0f; // top-left corner from the pen on the baseline, in pixels
    float offset_y = 0.0f;
    std::vector<std::uint8_t> pixels;
};

class SystemFonts
{
  public:
    SystemFonts();
    ~SystemFonts();
    SystemFonts(const SystemFonts &) = delete;
    SystemFonts &operator=(const SystemFonts &) = delete;

    void set(std::vector<std::string> files, std::string_view language);
    bool empty() const
    {
        return faces_.empty();
    }
    // The first face that has the character (reading font files until one does), or -1.
    int face_for(char32_t c);
    bool face_has(int face, char32_t c);
    // Glyphs of `count` characters that run one way, in drawing order (left to right).
    void shape(int face, const char32_t *text, int count, bool right_to_left, std::vector<RunGlyph> *out);
    // The glyph's distance field with the em drawn at pixel_size; false when it has no outline
    // that can be drawn.
    bool field(int face, std::uint32_t glyph, float pixel_size, float range, GlyphField *out);
    // What was read and drawn, for the log:
    // "SSTJpPro-Regular.otf (2965828 bytes), ...; 412 glyphs drawn in 96 ms".
    std::string loaded() const;

  private:
    struct Face;
    bool ready(int face);

    std::vector<std::unique_ptr<Face>> faces_;
    std::unordered_map<char32_t, std::int8_t> face_of_;
    std::string language_;
    void *buffer_ = nullptr; // hb_buffer_t, reused
    std::size_t fields_ = 0;
    double field_seconds_ = 0.0;
    double read_seconds_ = 0.0;
};

} // namespace pe::gfx
