// ProsperoEden - The launcher: home, library, settings and their dialogs.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/audio/sounds.hpp"
#include "pe/ui/widgets.hpp"

#include <array>
#include <future>
#include <string>
#include <vector>

namespace pe::ui
{

// Every screen of the launcher as one state machine. The frontend feeds it
// pressed buttons and frame time, draws its list and plays its cues; it ends
// when a game is chosen (and the closing animation has run).
class Launcher
{
  public:
    // first_start: the app just opened (false after returning from a game).
    Launcher(Services &services, Textures &textures, const Fonts &fonts, bool first_start);
    ~Launcher();
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    void press(Key key);
    void update(float dt);
    void draw(gfx::DrawList &list);

    // The sounds asked for since the last call.
    std::vector<audio::Cue> take_cues();
    // Launcher sound level (0-100) as set in Settings > Audio.
    int menu_volume() const
    {
        return prefs_.menu_volume;
    }
    bool done() const
    {
        return done_;
    }
    // Empty until a game is chosen.
    const std::string &selected_game() const
    {
        return selected_game_;
    }

  private:
    enum class Screen : std::uint8_t
    {
        home,
        library,
        settings,
        files,
        language,
        about,
    };
    enum class Modal : std::uint8_t
    {
        none,
        video,
        audio,
        controls,
        accessibility,
        diagnostics,
        game,
        mods, // a game's mods, opened from its settings
    };

    // ---- shell (launcher.cpp) ----
    void cue(audio::Cue value)
    {
        cues_.push_back(value);
    }
    void open(Screen screen, bool forward);
    void open_modal(Modal modal);
    void close_modal();
    void say(const std::string &text, bool warning = false);
    void launch(const std::string &file, const std::string &title, const std::string &cover);
    void draw_screen(Canvas &c, Screen screen);
    void draw_frame(Canvas &c, const char *title, const char *copy);
    void draw_footer(Canvas &c, const Hint *hints, int count);
    void draw_launch(Canvas &c);
    // quiet: a change that shows at once needs no "Saved" line.
    bool save_preferences(bool quiet = false);
    // The switches of a dialog as the preferences have them, in the order of its rows.
    std::array<bool, 3> switch_states(Modal modal) const;
    // Shows the launcher as the preferences' accessibility switches say.
    void apply_look();

    // ---- home (home.cpp) ----
    void press_home(Key key);
    void update_controllers(float dt);
    void draw_home(Canvas &c);
    void draw_controllers(Canvas &c);
    void open_library_at_last();

    // ---- library and game settings (library.cpp) ----
    // The game list is read beside the menu: reading every game takes a moment.
    void start_scan();
    void finish_scan(bool wait);
    void apply_games(std::vector<Game> games);
    void name_home_games();
    // The home screen's content, with its game's mods counted.
    void read_home();
    // A game's mods as its list has them: how many, and how many are switched on.
    void count_mods(Game &game, const std::vector<Mod> &mods);
    // What a game comes with, on one line: "Update 1.2.0, 2 DLC, 2 mods"; "None" without any.
    static std::string addons_line(const std::string &addons, int mods, int mods_on);
    // A refresh rate setting as the player reads it: 0 is "60 Hz", 1 is "120 Hz".
    static std::string hertz(int refresh);
    void enter_library();
    void press_library(Key key);
    void draw_library(Canvas &c);
    void refresh_selected_game();
    void press_game(Key key);
    void draw_game(Canvas &c, float open);
    void press_mods(Key key);
    void draw_mods(Canvas &c, float open);

    // ---- settings and its dialogs (settings.cpp) ----
    void press_settings(Key key);
    void draw_settings(Canvas &c);
    void press_dialog(Key key);
    void draw_dialog(Canvas &c, Modal modal, float open);
    int dialog_rows(Modal modal) const;
    float dialog_row_top(Modal modal, int row) const;

    // ---- game files, language, about (browse.cpp) ----
    void enter_files();
    bool browse_to(const std::string &directory);
    void press_files(Key key);
    void draw_files(Canvas &c);
    void enter_language();
    void press_language(Key key);
    void draw_language(Canvas &c);
    void draw_about(Canvas &c);

    Services &services_;
    Textures &textures_;
    Fonts fonts_;
    Backdrop backdrop_;
    std::vector<audio::Cue> cues_;
    float time_ = 0.0f;
    float clock_wait_ = 0.0f;
    std::string clock_;
    std::string version_;

    // navigation
    Screen screen_ = Screen::home;
    Screen leaving_ = Screen::home;
    tween::Timer transition_;
    bool forward_ = true;
    Modal modal_ = Modal::none;
    Modal modal_shown_ = Modal::none; // still drawn while it closes
    tween::Spring modal_open_;
    tween::Spring dim_;   // darkness over the art behind full screens
    float press_ = 0.0f;  // 1 at a confirm, then decays: the focused item dips
    std::string message_; // the last result ("Saved..."), shown where the screen has room
    bool message_warning_ = false;
    float message_age_ = 0.0f;

    // launching a game
    std::string selected_game_;
    std::string launch_title_;
    std::string launch_cover_;
    tween::Timer launch_;
    bool done_ = false;

    // home: 0 continue, 1-3 header, 4 game details, 5-8 recent, 9 view all
    Home home_;
    // The controllers connected now (bit 0 is player 1), and how lit each one's icon is.
    unsigned controllers_ = 0;
    bool controllers_known_ = false;
    std::array<tween::Spring, 4> controller_lit_{};
    std::array<float, 4> controller_pop_{}; // 1 when a controller appears, then decays
    int home_focus_ = 0;
    std::array<tween::Spring, 10> home_springs_{};
    float intro_ = 0.0f;
    bool first_start_ = true;

    // library
    std::vector<Game> games_;
    std::future<std::vector<Game>> scan_; // the list being read
    bool games_loaded_ = false;
    ListView library_;
    bool selected_docked_ = true;
    tween::Spring mode_;   // 0 docked .. 1 handheld
    tween::Spring detail_; // the details fade in after the selection moves

    // settings and dialogs
    Preferences prefs_;
    ListView settings_;
    tween::Spring section_;
    int option_ = 0;
    tween::Spring option_cursor_; // highlight position in pixels
    std::array<tween::Spring, 4> switches_{};
    GameSettings game_settings_;
    bool game_docked_ = true;
    SaveSource import_source_ = SaveSource::none; // what Save data could import for the game
    bool import_armed_ = false;                   // Cross was pressed once: the next one imports
    ListView game_rows_;                          // the game dialog's rows (more than it shows)
    std::vector<Mod> mods_;                       // the game's mods, read when its dialog opens
    ListView mod_rows_;

    // game files
    std::string browse_dir_;
    std::vector<std::string> browse_entries_; // ".." first unless at "/", then subfolders
    ListView files_;
    FolderInfo folder_info_;

    // language
    ListView language_;
};

} // namespace pe::ui
