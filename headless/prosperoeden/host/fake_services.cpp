// ProsperoEden - Sample data for the launcher preview on a PC (no console, no game files).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fake_services.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pe::host
{

namespace
{

struct Sample
{
    const char *name;
    const char *format;
    const char *size;
    const char *addons;
    const char *language;
    const char *note;
    std::uint32_t sky;  // cover colours
    std::uint32_t land;
    std::uint32_t mark;
};

// Invented titles: nothing here names a real game.
constexpr Sample kSamples[] = {
    {"Starfall Odyssey", "NSP", "6.4 GB", "Update 1.2.0, 2 DLC", "English (US)", "", 0x1b2a6b, 0x40b3c8, 0xffd166},
    {"Moss & Lantern", "XCI", "2.1 GB", "None", "English (US)", "", 0x16402f, 0x7bc86c, 0xf4e285},
    {"Kart Carnival Deluxe", "NSP", "7.8 GB", "Update 3.0.1, 48 DLC", "English (US)", "", 0xb3261e, 0xffb238, 0xffffff},
    {"Tiny Harbor", "NSP", "512.0 MB", "None", "English (US)", "", 0x256d8f, 0x9bd8e6, 0xfff3d6},
    {"Echoes of the Valley", "XCI", "14.2 GB", "Update 1.1.0", "Spanish",
     "Portuguese (Brazil) not available", 0x3b1f5e, 0xc77dff, 0xffe0f5},
    {"Pocket Rally Turbo", "NSP", "1.9 GB", "None", "English (US)", "", 0x202020, 0xe85d04, 0xf8f9fa},
    {"Cloudline", "NSP", "3.3 GB", "1 DLC", "English (US)", "", 0x5fa8d3, 0xcae9ff, 0x1b4965},
    {"Ember Knights II", "XCI", "9.6 GB", "Update 2.4.0, 5 DLC", "English (US)", "", 0x3d0c02, 0xd62828, 0xfcbf49},
    {"Paper Garden", "NSP", "840.5 MB", "None", "English (US)", "", 0xf1e3c6, 0x90be6d, 0x386641},
    {"Neon Drifters", "NSP", "5.2 GB", "Update 1.0.3", "English (US)", "", 0x10002b, 0x7b2cbf, 0x5ef2ff},
    {"Caf\xC3\xA9 Nocturne", "NSP", "2.7 GB", "None", "French", "", 0x2b1d0e, 0xa9713c, 0xf6e7cb},
    {"Sky Shepherds", "XCI", "4.4 GB", "3 DLC", "English (US)", "", 0x457b9d, 0xa8dadc, 0xf1faee},
};

void put_pixel(std::vector<std::uint8_t> &pixels, int size, int x, int y, float r, float g, float b)
{
    std::uint8_t *out = pixels.data() + (static_cast<std::size_t>(y) * size + x) * 4;
    out[0] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
    out[1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
    out[2] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
    out[3] = 255;
}

// A simple poster: sky gradient, a sun or moon, two ranges of hills.
bool write_cover(const std::string &path, const Sample &sample, int index)
{
    constexpr int kSize = 256;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kSize) * kSize * 4);
    const auto channel = [](std::uint32_t color, int shift)
    { return static_cast<float>((color >> shift) & 0xff) / 255.0f; };
    const float sky[3] = {channel(sample.sky, 16), channel(sample.sky, 8), channel(sample.sky, 0)};
    const float land[3] = {channel(sample.land, 16), channel(sample.land, 8), channel(sample.land, 0)};
    const float mark[3] = {channel(sample.mark, 16), channel(sample.mark, 8), channel(sample.mark, 0)};
    const float sun_x = 60.0f + static_cast<float>((index * 53) % 140);
    const float sun_y = 70.0f + static_cast<float>((index * 31) % 50);
    const float sun_r = 26.0f + static_cast<float>((index * 7) % 14);
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            const float v = static_cast<float>(y) / (kSize - 1);
            float color[3];
            for (int k = 0; k < 3; ++k)
                color[k] = sky[k] * (1.0f - v * 0.55f) + land[k] * v * 0.35f;
            const float distance = std::hypot(static_cast<float>(x) - sun_x, static_cast<float>(y) - sun_y);
            const float disc = std::clamp(sun_r - distance + 0.5f, 0.0f, 1.0f);
            const float halo = std::exp(-distance / (sun_r * 1.6f)) * 0.35f;
            for (int k = 0; k < 3; ++k)
                color[k] = color[k] * (1.0f - disc) + mark[k] * disc + mark[k] * halo;
            const float fx = static_cast<float>(x);
            const float far_hill = 168.0f + 18.0f * std::sin(fx * 0.031f + index) +
                                   9.0f * std::sin(fx * 0.083f + index * 2.0f);
            const float near_hill = 204.0f + 22.0f * std::sin(fx * 0.024f + index * 1.7f + 2.0f) +
                                    7.0f * std::sin(fx * 0.11f + index);
            if (static_cast<float>(y) > far_hill)
                for (int k = 0; k < 3; ++k)
                    color[k] = land[k] * 0.72f + sky[k] * 0.18f;
            if (static_cast<float>(y) > near_hill)
                for (int k = 0; k < 3; ++k)
                    color[k] = land[k] * 0.38f + sky[k] * 0.10f;
            put_pixel(pixels, kSize, x, y, color[0], color[1], color[2]);
        }
    }
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;
    const unsigned char header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      kSize & 0xff, kSize >> 8, kSize & 0xff, kSize >> 8, 32, 0x28};
    const bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
                    std::fwrite(pixels.data(), 1, pixels.size(), file) == pixels.size();
    std::fclose(file);
    return ok;
}

const std::vector<std::string> kResolutionLabels = {"0.5x (faster, softer)", "0.75x (faster)",
                                                    "1x (native)", "1.5x (sharper)",
                                                    "2x (sharpest)"};
const std::vector<std::string> kResolutionKeys = {"0.5x", "0.75x", "1x", "1.5x", "2x"};
const std::vector<std::string> kFilterLabels = {"Bilinear", "AMD FSR", "Bicubic", "Nearest"};
const std::vector<std::string> kLanguageLabels = {
    "English (US)", "English (UK)", "French", "French (Canada)", "German", "Italian", "Spanish",
    "Spanish (Latin America)", "Portuguese", "Portuguese (Brazil)", "Dutch", "Russian", "Polish",
    "Japanese", "Korean", "Chinese (Simplified)", "Chinese (Traditional)", "Thai"};
constexpr int kLanguageRegions[] = {1, 2, 2, 1, 2, 2, 2, 1, 2, 1, 2, 2, 2, 0, 5, 4, 6, 1};

} // namespace

FakeServices::FakeServices(const std::string &covers_directory)
{
    int index = 0;
    for (const Sample &sample : kSamples)
    {
        char id[32];
        std::snprintf(id, sizeof(id), "0100%04X0000%04X", 0xA000 + index, 0x1000 * (index % 8));
        ui::Game game;
        game.name = sample.name;
        game.format = sample.format;
        game.size = sample.size;
        game.file = std::string(sample.name) + " [" + id + "]." +
                    (std::string(sample.format) == "NSP" ? "nsp" : "xci");
        game.title_id = 0x0100A00000001000ull + static_cast<std::uint64_t>(index) * 0x10000;
        game.addons = sample.addons;
        game.language = sample.language;
        game.language_note = sample.note;
        // One game has no cover art, to show the placeholder.
        if (index != 8)
        {
            game.cover = covers_directory + "/cover-" + std::to_string(index) + ".tga";
            if (!write_cover(game.cover, sample, index))
                game.cover.clear();
        }
        games_.push_back(game);
        ++index;
    }
    std::sort(games_.begin(), games_.end(),
              [](const ui::Game &a, const ui::Game &b) { return a.name < b.name; });
}

ui::Home FakeServices::home()
{
    ui::Home home;
    home.setup_ready = setup_ready;
    if (!setup_ready)
        home.status = "Setup required: prod.keys is missing. Open Settings, Game files to choose the "
                      "folder that holds your keys, firmware and roms folders (or add the files to "
                      "/data/prosperoeden), then reopen ProsperoEden.";
    else if (!launch_error.empty())
    {
        home.status = "Game could not start: " + launch_error +
                      " Details: /data/prosperoeden/logs/stderr.log";
        home.launch_failed = true;
    }
    if (has_history && setup_ready)
    {
        const auto find = [&](const char *name) -> const ui::Game &
        {
            for (const ui::Game &game : games_)
                if (game.name == name)
                    return game;
            return games_.front();
        };
        const ui::Game &last = find("Echoes of the Valley");
        home.last_file = last.file;
        home.last_exists = true;
        home.last_title = last.name;
        home.last_caption = "Last launched Sep 30 at 20:14";
        home.last_cover = last.cover;
        home.last_info = "Add-ons: " + last.addons + "  /  Language: " + last.language + " (" +
                         last.language_note + " in this game)";
        home.last_info_warning = true;
        for (const char *name : {"Echoes of the Valley", "Kart Carnival Deluxe", "Starfall Odyssey",
                                 "Caf\xC3\xA9 Nocturne"})
        {
            const ui::Game &game = find(name);
            home.recents.push_back({game.file, game.name, game.cover});
        }
    }
    home.system_status = std::to_string(games_.size()) + " games installed  /  " +
                         (setup_ready ? "Firmware ready" : "Setup required");
    return home;
}

bool FakeServices::docked(std::uint64_t title_id)
{
    return std::find(handheld_.begin(), handheld_.end(), title_id) == handheld_.end();
}

bool FakeServices::set_docked(std::uint64_t title_id, bool docked)
{
    handheld_.erase(std::remove(handheld_.begin(), handheld_.end(), title_id), handheld_.end());
    if (!docked)
        handheld_.push_back(title_id);
    return true;
}

const std::vector<std::string> &FakeServices::resolution_labels()
{
    return kResolutionLabels;
}
const std::vector<std::string> &FakeServices::resolution_keys()
{
    return kResolutionKeys;
}
const std::vector<std::string> &FakeServices::filter_labels()
{
    return kFilterLabels;
}
const std::vector<std::string> &FakeServices::language_labels()
{
    return kLanguageLabels;
}

std::string FakeServices::language_region(int language)
{
    static constexpr const char *kRegions[] = {"Japan", "USA", "Europe", "Australia",
                                               "China", "Korea", "Taiwan"};
    return language >= 0 && language < 18 ? kRegions[kLanguageRegions[language]] : "";
}

std::string FakeServices::setup_details()
{
    return setup_ready ? "Keys and firmware: startup checks passed. Game-specific compatibility is "
                         "checked at launch." :
                         "prod.keys is missing.";
}

bool FakeServices::folders(const std::string &directory, std::vector<std::string> *names)
{
    names->clear();
    if (directory == "/mnt/ext1/eden")
        *names = {"firmware", "keys", "roms", "updates"};
    else if (directory == "/mnt/ext1")
        *names = {"backups", "eden", "media", "music", "photos", "retro", "saves", "videos"};
    else if (directory == "/mnt")
        *names = {"ext0", "ext1", "usb0"};
    else if (directory == "/")
        *names = {"data", "mnt", "user"};
    else if (directory == "/data")
        *names = {"homebrew", "prosperoeden"};
    else if (directory == "/data/prosperoeden")
        *names = {"config", "covers", "logs"};
    return true;
}

ui::FolderInfo FakeServices::folder_info(const std::string &directory)
{
    ui::FolderInfo info;
    if (directory == "/mnt/ext1/eden")
    {
        info.keys = true;
        info.firmware = 236;
        info.games = static_cast<int>(games_.size());
    }
    return info;
}

} // namespace pe::host
