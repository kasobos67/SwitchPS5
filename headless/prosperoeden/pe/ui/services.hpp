// ProsperoEden - What the launcher screens ask of the application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/gfx/image.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pe::ui
{

// One pressed button (a first press, or a repeat of a held direction or shoulder button).
enum class Key : std::uint8_t
{
    cross,
    circle,
    square,
    triangle,
    options,
    l1,
    r1,
    up,
    down,
    left,
    right,
};

// A game file in the games folder.
struct Game
{
    std::string name;
    std::string format; // "NSP" or "XCI"
    std::string size;   // "1.2 GB"
    std::string file;   // its name in the games folder
    std::string cover;  // image path; empty without cover art
    std::uint64_t title_id = 0;
    std::string addons;        // "Update 1.2.0, 2 DLC"; empty without either
    std::string language;      // the language the game will use
    std::string language_note; // set when that is not the chosen one
    // Its mods, and how many of them are switched on. The launcher counts them (Services::mods)
    // when it takes the list and whenever they change.
    int mods = 0;
    int mods_on = 0;
};

struct Recent
{
    std::string file;
    std::string title;
    std::string cover;
};

// The home screen's content.
struct Home
{
    bool setup_ready = false;
    std::string status;    // a setup or launch problem; empty when there is none
    bool launch_failed = false; // the status is about the game that just failed to start
    std::string last_file; // the last game played; empty before the first one
    bool last_exists = false;
    std::string last_title;
    std::string last_caption;
    bool last_caption_warning = false; // the caption says what is wrong with the game
    std::string last_cover;
    // What the last game comes with, when it can be started: its title ID, its update and DLC
    // (as Game::addons) and the language it will use. Its mods are counted by the launcher.
    std::uint64_t last_title_id = 0;
    std::string last_addons;
    std::string last_language;
    int last_mods = 0;
    int last_mods_on = 0;
    std::vector<Recent> recents; // at most four
    std::string system_status;
};

struct Preferences
{
    bool hud = true;
    int volume = 100; // game volume, 0-100
    bool mute = false;
    bool detailed_logging = false;
    int renderer = 1; // 0 OpenGL, 1 Vulkan
    int resolution = 2;
    int filter = 0;
    int refresh = 0; // the output while a game runs: 0 60 Hz, 1 120 Hz
    bool vibration = true;
    int language = 0;
    int menu_volume = 70; // launcher sounds, 0-100
    // Accessibility: how the launcher itself is shown (theme.hpp, Look).
    bool large_text = false;
    bool high_contrast = false;
    bool reduce_motion = false;
};

// One game's overrides; -1 uses Settings > Video.
struct GameSettings
{
    int renderer = -1;
    int resolution = -1;
    int filter = -1;
    int refresh = -1;
};

// A mod of one game, from the game files folder's mods/<title ID>/.
struct Mod
{
    std::string name;    // its folder's name
    std::string kind;    // what it is made of: "Patch", "Files", "Cheats"
    bool enabled = true; // used when the game starts
};

// Where a save to import was found, in the game files folder.
enum class SaveSource : std::uint8_t
{
    none,
    folder,  // save-import/<title ID>/, copied by hand
    ryujinx, // ryujinx/, a Ryujinx data folder
};

// What a folder holds, for Settings > Game files. Counts are -1 without the subfolder.
struct FolderInfo
{
    bool keys = false;
    int firmware = -1;
    int games = -1;
};

class Services
{
  public:
    virtual ~Services() = default;

    // ---- home ----
    virtual Home home() = 0;
    virtual std::string clock() = 0;   // "14:05"
    // The players (bit 0 is player 1) whose controller is connected right now.
    virtual unsigned controllers()
    {
        return 1u;
    }
    virtual std::string version() = 0; // "v1.000.040"

    // ---- library ----
    virtual std::vector<Game> games() = 0; // reads every game file: slow
    // The value the launcher hands back to start a game.
    virtual std::string game_path(const std::string &file) = 0;
    virtual bool docked(std::uint64_t title_id) = 0;
    virtual bool set_docked(std::uint64_t title_id, bool docked) = 0;
    virtual GameSettings game_settings(std::uint64_t title_id) = 0;
    virtual bool set_game_settings(std::uint64_t title_id, const GameSettings &settings) = 0;

    // ---- settings ----
    virtual Preferences preferences() = 0;
    virtual bool set_preferences(const Preferences &preferences) = 0;
    virtual const std::vector<std::string> &resolution_labels() = 0; // "1x (native)"
    virtual const std::vector<std::string> &resolution_keys() = 0;   // "1x"
    virtual const std::vector<std::string> &filter_labels() = 0;
    virtual const std::vector<std::string> &language_labels() = 0;
    virtual std::string language_region(int language) = 0;
    virtual std::string setup_details() = 0;

    // ---- game files ----
    // The subfolder names of a folder; false when it cannot be opened.
    virtual bool folders(const std::string &directory, std::vector<std::string> *names) = 0;
    virtual FolderInfo folder_info(const std::string &directory) = 0;
    virtual std::string files_folder() = 0;       // in use by this process
    virtual std::string saved_files_folder() = 0; // used from the next start; empty when none
    virtual std::string default_files_folder() = 0;
    virtual bool set_files_folder(const std::string &directory) = 0;
    virtual int filesystem_access() = 0; // 0: the whole filesystem

    // ---- save transfer: a game's save in from, or out to, a folder (not in every build) ----
    virtual bool save_transfer_available()
    {
        return false;
    }
    // What there is to import for the game.
    virtual SaveSource save_import_source(std::uint64_t)
    {
        return SaveSource::none;
    }
    // Each returns whether it was done, with what to tell the player in message.
    virtual bool save_import(std::uint64_t, std::string *)
    {
        return false;
    }
    virtual bool save_export(std::uint64_t, std::string *)
    {
        return false;
    }

    // ---- mods: patches, replacement files and cheats the player added for a game ----
    virtual std::vector<Mod> mods(std::uint64_t)
    {
        return {};
    }
    virtual bool set_mod_enabled(std::uint64_t, const std::string &, bool)
    {
        return false;
    }
    // Where a game's mods go, as the player would write it: "mods/0100.../".
    virtual std::string mods_folder(std::uint64_t)
    {
        return {};
    }
    // Makes that folder; false when it cannot be made.
    virtual bool make_mods_folder(std::uint64_t)
    {
        return false;
    }

    // ---- images ----
    virtual bool load_image(const std::string &path, gfx::Image *image) = 0;
};

} // namespace pe::ui
