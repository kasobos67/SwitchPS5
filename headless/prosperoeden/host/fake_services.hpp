// ProsperoEden - Sample data for the launcher preview on a PC (no console, no game files).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/core/strings.hpp"
#include "pe/ui/services.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace pe::host
{

// A made-up library: invented titles with drawn covers, settings kept in memory.
class FakeServices final : public ui::Services
{
  public:
    // covers_directory receives one drawn cover per sample game.
    explicit FakeServices(const std::string &covers_directory);

    // What the preview varies between pictures.
    bool setup_ready = true;
    std::string launch_error;
    std::string crash_report; // the previous run's crash report, when it left one
    bool has_history = true;
    bool import_available = true;
    ui::SaveSource import_source = ui::SaveSource::ryujinx;
    unsigned connected_controllers = 0b0011;

    ui::Home home() override;
    std::string clock() override
    {
        return "21:47";
    }
    unsigned controllers() override
    {
        return connected_controllers;
    }
    std::string version() override
    {
        return "v1.000.040";
    }
    // Game files the preview takes away from the folder, as a player would by deleting them.
    std::vector<std::string> removed;
    bool game_exists(const std::string &file) override
    {
        return std::find(removed.begin(), removed.end(), file) == removed.end();
    }
    std::vector<ui::Game> games() override
    {
        std::vector<ui::Game> present;
        for (const ui::Game &game : games_)
            if (game_exists(game.file))
                present.push_back(game);
        return present;
    }
    std::string game_path(const std::string &file) override
    {
        return "/games/roms/" + file;
    }
    bool docked(std::uint64_t title_id) override;
    bool set_docked(std::uint64_t title_id, bool docked) override;
    ui::GameSettings game_settings(std::uint64_t) override
    {
        return game_settings_;
    }
    bool set_game_settings(std::uint64_t, const ui::GameSettings &settings) override
    {
        game_settings_ = settings;
        return true;
    }
    ui::Preferences preferences() override
    {
        return preferences_;
    }
    bool set_preferences(const ui::Preferences &preferences) override
    {
        preferences_ = preferences;
        return true;
    }
    const std::vector<std::string> &resolution_labels() override;
    const std::vector<std::string> &resolution_keys() override;
    const std::vector<std::string> &filter_labels() override;
    const std::vector<std::string> &language_labels() override;
    std::string language_region(int language) override;
    std::string setup_details() override;
    bool folders(const std::string &directory, std::vector<std::string> *names) override;
    ui::FolderInfo folder_info(const std::string &directory) override;
    std::string files_folder() override
    {
        return "/mnt/ext1/eden";
    }
    std::string saved_files_folder() override
    {
        return saved_folder_;
    }
    std::string default_files_folder() override
    {
        return "/data/prosperoeden";
    }
    bool set_files_folder(const std::string &directory) override
    {
        saved_folder_ = directory;
        return true;
    }
    int filesystem_access() override
    {
        return 0;
    }
    bool save_transfer_available() override
    {
        return import_available;
    }
    ui::SaveSource save_import_source(std::uint64_t) override
    {
        return import_source;
    }
    bool save_import(std::uint64_t, std::string *message) override
    {
        *message = tr("Imported. The save it replaced was backed up.");
        return true;
    }
    bool save_export(std::uint64_t, std::string *message) override
    {
        *message = fill(tr("Exported to {0}."), {"save-export/0100A00B00003000-20261001-213000"});
        return true;
    }
    // Sample mods, for two of the games: three, the second switched off, the third with cheats
    // chosen one by one (its two frame rates replace each other); none when has_mods is cleared.
    bool has_mods = true;
    std::vector<ui::Mod> mods(std::uint64_t) override;
    bool set_mod_enabled(std::uint64_t, const std::string &name, bool enabled) override;
    bool set_cheat_enabled(std::uint64_t, const std::string &mod, const std::string &cheat,
                           bool enabled) override;
    // One Mods switch for every sample game.
    bool mods_enabled(std::uint64_t) override
    {
        return mods_enabled_;
    }
    bool set_mods_enabled(std::uint64_t, bool enabled) override
    {
        mods_enabled_ = enabled;
        return true;
    }
    std::string mods_folder(std::uint64_t) override
    {
        return "mods/0100A00B00003000/";
    }
    bool make_mods_folder(std::uint64_t) override
    {
        return true;
    }
    bool load_image(const std::string &path, gfx::Image *image) override
    {
        return gfx::load_tga(path, image);
    }

  private:
    std::vector<ui::Game> games_;
    std::vector<std::uint64_t> handheld_;
    ui::GameSettings game_settings_;
    ui::Preferences preferences_;
    std::string saved_folder_;
    std::vector<std::uint64_t> modded_; // the games that have the sample mods
    std::vector<std::string> mods_off_{"Sharper textures"};
    std::vector<std::string> cheats_on_{"60 FPS"};
    bool mods_enabled_ = true;
};

} // namespace pe::host
