// ProsperoEden - Sample data for the launcher preview on a PC (no console, no game files).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/core/strings.hpp"
#include "pe/ui/services.hpp"

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
        return "v1.000.030";
    }
    std::vector<ui::Game> games() override
    {
        return games_;
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
};

} // namespace pe::host
