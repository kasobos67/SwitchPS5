// ProsperoEden - RGBA images for textures: TGA files and box downsampling.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/gfx/image.hpp"

#include <cstdio>

namespace pe::gfx
{

bool load_tga(const std::string &path, Image *image)
{
    *image = Image{};
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    unsigned char header[18]{};
    bool ok = std::fread(header, 1, sizeof(header), file) == sizeof(header);
    const int width = header[12] | (header[13] << 8);
    const int height = header[14] | (header[15] << 8);
    const int bytes = header[16] / 8;
    // No colour map, uncompressed true colour, at most 8192 pixels a side.
    ok = ok && header[1] == 0 && header[2] == 2 && (bytes == 3 || bytes == 4) && width > 0 &&
         height > 0 && width <= 8192 && height <= 8192;
    ok = ok && std::fseek(file, header[0], SEEK_CUR) == 0; // image ID field
    std::vector<std::uint8_t> raw;
    if (ok)
    {
        raw.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                   static_cast<std::size_t>(bytes));
        ok = std::fread(raw.data(), 1, raw.size(), file) == raw.size();
    }
    std::fclose(file);
    if (!ok)
        return false;

    const bool top_first = (header[17] & 0x20) != 0;
    image->width = width;
    image->height = height;
    image->rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y)
    {
        const int source_row = top_first ? y : height - 1 - y;
        const std::uint8_t *in =
            raw.data() + static_cast<std::size_t>(source_row) * static_cast<std::size_t>(width) *
                             static_cast<std::size_t>(bytes);
        std::uint8_t *out =
            image->rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        for (int x = 0; x < width; ++x, in += bytes, out += 4)
        {
            out[0] = in[2];
            out[1] = in[1];
            out[2] = in[0];
            out[3] = bytes == 4 ? in[3] : 255;
        }
    }
    return true;
}

Image halve(const Image &image)
{
    Image result;
    result.width = image.width / 2;
    result.height = image.height / 2;
    if (result.empty())
        return Image{};
    result.rgba.resize(static_cast<std::size_t>(result.width) *
                       static_cast<std::size_t>(result.height) * 4);
    const std::size_t stride = static_cast<std::size_t>(image.width) * 4;
    for (int y = 0; y < result.height; ++y)
    {
        const std::uint8_t *row = image.rgba.data() + static_cast<std::size_t>(y) * 2 * stride;
        std::uint8_t *out =
            result.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(result.width) * 4;
        for (int x = 0; x < result.width; ++x, out += 4)
        {
            const std::uint8_t *block[4] = {row + static_cast<std::size_t>(x) * 8,
                                            row + static_cast<std::size_t>(x) * 8 + 4,
                                            row + stride + static_cast<std::size_t>(x) * 8,
                                            row + stride + static_cast<std::size_t>(x) * 8 + 4};
            unsigned alpha = 0;
            unsigned color[3] = {0, 0, 0};
            for (const std::uint8_t *pixel : block)
            {
                alpha += pixel[3];
                for (int channel = 0; channel < 3; ++channel)
                    color[channel] += static_cast<unsigned>(pixel[channel]) * pixel[3];
            }
            for (int channel = 0; channel < 3; ++channel)
                out[channel] =
                    static_cast<std::uint8_t>(alpha != 0 ? (color[channel] + alpha / 2) / alpha : 0);
            out[3] = static_cast<std::uint8_t>((alpha + 2) / 4);
        }
    }
    return result;
}

} // namespace pe::gfx
