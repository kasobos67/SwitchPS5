// ProsperoEden - The console's own fonts, for the scripts the launcher's baked font lacks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/gfx/system_fonts.hpp"

#include "pe/core/file.hpp"

#include <hb.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pe::gfx
{

namespace
{

// The scripts the console has a font of its own for.
enum class Script : std::uint8_t
{
    other,
    japanese,
    korean,
    chinese,
    thai,
    arabic,
};

struct Group
{
    Script script;
    // The same font in the weights and generations a system software version may carry; the
    // first one found is used.
    const char *files[3];
};
constexpr Group kLatin{Script::other, {"SST-Medium.otf", "SST-Roman.otf", nullptr}};
constexpr Group kJapanese{Script::japanese, {"SSTJpPro-Regular.otf", "SIE-RDC-Pr5N-M-JPN.otf", "SCE-RDC-R-JPN.otf"}};
constexpr Group kKorean{Script::korean, {"YoonGothicProSIE760.otf", "SCEPS4Yoongd-Medium.otf", nullptr}};
constexpr Group kChinese{Script::chinese, {"DFHEI5-SONY.ttf", nullptr, nullptr}};
constexpr Group kThai{Script::thai, {"SSTThai-Medium.otf", "SSTThai-Roman.otf", nullptr}};
constexpr Group kArabic{Script::arabic, {"SSTArabic-Medium.otf", "SSTArabic-Roman.otf", nullptr}};

// A character only one of the fonts should draw, whatever their order.
Script own_script(char32_t c)
{
    if ((c >= 0x0600 && c <= 0x06ff) || (c >= 0x0750 && c <= 0x077f) || (c >= 0xfb50 && c <= 0xfdff) ||
        (c >= 0xfe70 && c <= 0xfeff))
        return Script::arabic;
    if (c >= 0x0e00 && c <= 0x0e7f)
        return Script::thai;
    if ((c >= 0xac00 && c <= 0xd7a3) || (c >= 0x1100 && c <= 0x11ff) || (c >= 0x3130 && c <= 0x318f))
        return Script::korean;
    if (c >= 0x3040 && c <= 0x30ff)
        return Script::japanese;
    return Script::other;
}

// Adds the time it lives to a total.
struct Stopwatch
{
    double *total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Stopwatch()
    {
        *total += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
};

bool exists(const std::string &path)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

// ---- distance fields ----
constexpr float kFar = 1e20f;

// One row or column of the squared Euclidean distance transform (Felzenszwalb and Huttenlocher):
// d[q] = min over p of (q - p)^2 + f[p].
void transform(const float *f, int count, float *d, int *v, float *z)
{
    // Where the parabolas rooted at p and q cross. Values are 0 or kFar, so this is finite and
    // above z[0]: the loop below always stops at k = 0.
    const auto crossing = [f](int p, int q)
    {
        return ((f[q] + static_cast<float>(q) * static_cast<float>(q)) -
                (f[p] + static_cast<float>(p) * static_cast<float>(p))) /
               static_cast<float>(2 * (q - p));
    };
    int k = 0;
    v[0] = 0;
    z[0] = -2.0f * kFar;
    z[1] = kFar;
    for (int q = 1; q < count; ++q)
    {
        float s = crossing(v[k], q);
        while (s <= z[k])
        {
            --k;
            s = crossing(v[k], q);
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = kFar;
    }
    k = 0;
    for (int q = 0; q < count; ++q)
    {
        while (z[k + 1] < static_cast<float>(q))
            ++k;
        const int p = v[k];
        d[q] = static_cast<float>((q - p) * (q - p)) + f[p];
    }
}

// grid holds 0 at the pixels distances are measured to and kFar elsewhere; on return, the squared
// distance from every pixel to the nearest of them.
void distances(std::vector<float> &grid, int width, int height)
{
    const int longest = std::max(width, height);
    std::vector<float> f(static_cast<std::size_t>(longest));
    std::vector<float> d(static_cast<std::size_t>(longest));
    std::vector<int> v(static_cast<std::size_t>(longest));
    std::vector<float> z(static_cast<std::size_t>(longest) + 1);
    for (int x = 0; x < width; ++x)
    {
        for (int y = 0; y < height; ++y)
            f[static_cast<std::size_t>(y)] = grid[static_cast<std::size_t>(y) * width + x];
        transform(f.data(), height, d.data(), v.data(), z.data());
        for (int y = 0; y < height; ++y)
            grid[static_cast<std::size_t>(y) * width + x] = d[static_cast<std::size_t>(y)];
    }
    for (int y = 0; y < height; ++y)
    {
        float *row = grid.data() + static_cast<std::size_t>(y) * width;
        std::copy(row, row + width, f.begin());
        transform(f.data(), width, d.data(), v.data(), z.data());
        std::copy(d.begin(), d.begin() + width, row);
    }
}

} // namespace

std::vector<std::string> system_font_folders()
{
    std::vector<std::string> folders;
    // The PC preview has no console: PE_SYSTEM_FONTS names a folder holding copies of its fonts.
    if (const char *folder = std::getenv("PE_SYSTEM_FONTS"); folder != nullptr && folder[0] != 0)
        folders.emplace_back(folder);
    folders.emplace_back("/preinst/common/font");
    return folders;
}

std::vector<std::string> system_font_files(std::string_view folder, std::string_view language)
{
    const std::string_view prefix = language.substr(0, 2);
    std::vector<const Group *> order;
    if (prefix == "ja")
        order = {&kJapanese, &kLatin, &kChinese, &kKorean, &kThai, &kArabic};
    else if (prefix == "ko")
        order = {&kKorean, &kLatin, &kJapanese, &kChinese, &kThai, &kArabic};
    else if (prefix == "zh")
        order = {&kChinese, &kLatin, &kJapanese, &kKorean, &kThai, &kArabic};
    else if (prefix == "th")
        order = {&kThai, &kLatin, &kJapanese, &kChinese, &kKorean, &kArabic};
    else if (prefix == "ar")
        order = {&kArabic, &kLatin, &kJapanese, &kChinese, &kKorean, &kThai};
    else
        order = {&kLatin, &kJapanese, &kChinese, &kKorean, &kThai, &kArabic};
    std::vector<std::string> files;
    for (const Group *group : order)
    {
        for (const char *name : group->files)
        {
            if (name == nullptr)
                break;
            const std::string path = std::string(folder) + "/" + name;
            if (exists(path))
            {
                files.push_back(path);
                break;
            }
        }
    }
    return files;
}

struct SystemFonts::Face
{
    std::string path;
    Script script = Script::other;
    std::string data;
    bool tried = false;
    bool ok = false;
    stbtt_fontinfo info{};
    hb_blob_t *blob = nullptr;
    hb_face_t *face = nullptr;
    hb_font_t *font = nullptr;
    float upem = 1000.0f;

    ~Face()
    {
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }
};

SystemFonts::SystemFonts() : buffer_(hb_buffer_create())
{
}

SystemFonts::~SystemFonts()
{
    hb_buffer_destroy(static_cast<hb_buffer_t *>(buffer_));
}

void SystemFonts::set(std::vector<std::string> files, std::string_view language)
{
    faces_.clear();
    face_of_.clear();
    language_ = std::string(language);
    for (std::string &path : files)
    {
        if (faces_.size() == 100)
            break;
        auto face = std::make_unique<Face>();
        const std::string name = path.substr(path.find_last_of('/') + 1);
        for (const Group *group : {&kJapanese, &kKorean, &kChinese, &kThai, &kArabic})
            for (const char *file : group->files)
                if (file != nullptr && name == file)
                    face->script = group->script;
        face->path = std::move(path);
        faces_.push_back(std::move(face));
    }
}

bool SystemFonts::ready(int index)
{
    Face &face = *faces_[static_cast<std::size_t>(index)];
    if (face.tried)
        return face.ok;
    face.tried = true;
    const Stopwatch reading{&read_seconds_};
    if (!pe::read_file(face.path, &face.data, 32u << 20) || face.data.size() < 12)
        return false;
    const auto *bytes = reinterpret_cast<const unsigned char *>(face.data.data());
    const int offset = stbtt_GetFontOffsetForIndex(bytes, 0);
    if (offset < 0 || stbtt_InitFont(&face.info, bytes, offset) == 0)
        return false;
    face.blob = hb_blob_create(face.data.data(), static_cast<unsigned>(face.data.size()),
                               HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    face.face = hb_face_create(face.blob, 0);
    face.upem = static_cast<float>(hb_face_get_upem(face.face));
    if (hb_face_get_glyph_count(face.face) == 0 || face.upem <= 0.0f)
        return false;
    face.font = hb_font_create(face.face);
    face.ok = true;
    return true;
}

bool SystemFonts::face_has(int face, char32_t c)
{
    if (face < 0 || face >= static_cast<int>(faces_.size()) || !ready(face))
        return false;
    hb_codepoint_t glyph = 0;
    return hb_font_get_nominal_glyph(faces_[static_cast<std::size_t>(face)]->font, c, &glyph) != 0;
}

int SystemFonts::face_for(char32_t c)
{
    if (const auto found = face_of_.find(c); found != face_of_.end())
        return found->second;
    int chosen = -1;
    const Script own = own_script(c);
    const int count = static_cast<int>(faces_.size());
    for (int face = 0; own != Script::other && face < count && chosen < 0; ++face)
        if (faces_[static_cast<std::size_t>(face)]->script == own && face_has(face, c))
            chosen = face;
    for (int face = 0; face < count && chosen < 0; ++face)
        if (face_has(face, c))
            chosen = face;
    face_of_.emplace(c, static_cast<std::int8_t>(chosen));
    return chosen;
}

void SystemFonts::shape(int face_index, const char32_t *text, int count, bool right_to_left,
                        std::vector<RunGlyph> *out)
{
    out->clear();
    if (count <= 0 || face_index < 0 || face_index >= static_cast<int>(faces_.size()) || !ready(face_index))
        return;
    const Face &face = *faces_[static_cast<std::size_t>(face_index)];
    auto *buffer = static_cast<hb_buffer_t *>(buffer_);
    hb_buffer_reset(buffer);
    static_assert(sizeof(hb_codepoint_t) == sizeof(char32_t));
    hb_buffer_add_codepoints(buffer, reinterpret_cast<const hb_codepoint_t *>(text), count, 0, count);
    hb_buffer_set_direction(buffer, right_to_left ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    if (!language_.empty())
        hb_buffer_set_language(buffer, hb_language_from_string(language_.c_str(), -1));
    // Joiners and other invisible characters do their work in shaping and leave no glyph.
    hb_buffer_set_flags(buffer, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
    hb_buffer_guess_segment_properties(buffer); // the script, from the text
    hb_shape(face.font, buffer, nullptr, 0);
    unsigned glyphs = 0;
    const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &glyphs);
    const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &glyphs);
    const float unit = 1.0f / face.upem;
    out->reserve(glyphs);
    for (unsigned i = 0; i < glyphs; ++i)
        out->push_back({infos[i].codepoint, static_cast<float>(positions[i].x_offset) * unit,
                        static_cast<float>(positions[i].y_offset) * unit,
                        static_cast<float>(positions[i].x_advance) * unit});
}

bool SystemFonts::field(int face_index, std::uint32_t glyph, float pixel_size, float range, GlyphField *out)
{
    *out = GlyphField{};
    if (face_index < 0 || face_index >= static_cast<int>(faces_.size()) || !ready(face_index))
        return false;
    const Face &face = *faces_[static_cast<std::size_t>(face_index)];
    const Stopwatch drawing{&field_seconds_};
    ++fields_;
    // The outline is filled at twice the size and measured there: distances to pixel centres
    // are then within a quarter of an output pixel of the true ones.
    constexpr int kSuper = 2;
    const float scale = stbtt_ScaleForMappingEmToPixels(&face.info, pixel_size) * static_cast<float>(kSuper);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(&face.info, static_cast<int>(glyph), scale, scale, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0)
        return true; // a space
    const int pad = (static_cast<int>(std::ceil(range)) + 1) * kSuper;
    const auto down = [](int value) { return static_cast<int>(std::floor(static_cast<float>(value) / kSuper)) * kSuper; };
    const int left = down(x0 - pad);
    const int top = down(y0 - pad);
    const int right = -down(-(x1 + pad));
    const int bottom = -down(-(y1 + pad));
    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024)
        return false;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<unsigned char> coverage(count, 0);
    stbtt_MakeGlyphBitmap(&face.info, coverage.data() + static_cast<std::size_t>(y0 - top) * width + (x0 - left),
                          x1 - x0, y1 - y0, width, scale, scale, static_cast<int>(glyph));
    std::vector<float> outside(count); // distance from a pixel outside the glyph to the glyph
    std::vector<float> inside(count);  // distance from a pixel inside the glyph to its outside
    for (std::size_t i = 0; i < count; ++i)
    {
        const bool in = coverage[i] >= 128;
        outside[i] = in ? 0.0f : kFar;
        inside[i] = in ? kFar : 0.0f;
    }
    distances(outside, width, height);
    distances(inside, width, height);

    out->w = width / kSuper;
    out->h = height / kSuper;
    out->offset_x = static_cast<float>(left) / kSuper;
    out->offset_y = static_cast<float>(top) / kSuper;
    out->pixels.resize(static_cast<std::size_t>(out->w) * static_cast<std::size_t>(out->h));
    const float per_pixel = 128.0f / range;
    for (int y = 0; y < out->h; ++y)
    {
        for (int x = 0; x < out->w; ++x)
        {
            float sum = 0.0f;
            for (int dy = 0; dy < kSuper; ++dy)
            {
                for (int dx = 0; dx < kSuper; ++dx)
                {
                    const std::size_t i =
                        static_cast<std::size_t>(y * kSuper + dy) * width + static_cast<std::size_t>(x * kSuper + dx);
                    // Whole pixels away from the outline, then the part of this pixel it covers.
                    const float covered = static_cast<float>(coverage[i]) / 255.0f;
                    sum += coverage[i] >= 128 ? std::sqrt(inside[i]) - 0.5f + (covered - 1.0f) :
                                                0.5f - std::sqrt(outside[i]) + covered;
                }
            }
            const float distance = sum / static_cast<float>(kSuper * kSuper * kSuper); // in output pixels
            out->pixels[static_cast<std::size_t>(y) * out->w + x] =
                static_cast<std::uint8_t>(std::clamp(128.0f + distance * per_pixel, 0.0f, 255.0f));
        }
    }
    return true;
}

std::string SystemFonts::loaded() const
{
    std::string text;
    for (const auto &face : faces_)
    {
        if (!face->ok)
            continue;
        if (!text.empty())
            text += ", ";
        text += face->path.substr(face->path.find_last_of('/') + 1) + " (" + std::to_string(face->data.size()) + " bytes)";
    }
    if (text.empty())
        return "none";
    char numbers[96];
    std::snprintf(numbers, sizeof(numbers), " read in %.0f ms; %zu glyphs drawn in %.0f ms", read_seconds_ * 1000.0,
                  fields_, field_seconds_ * 1000.0);
    return text + numbers;
}

} // namespace pe::gfx
