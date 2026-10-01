// ProsperoEden - Launcher library: the game list, its details and per-game settings.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include "pe/core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

namespace pe::ui
{

using audio::Cue;

namespace
{

constexpr Rect kListPanel{108.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kDetailPanel{980.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kWindow{138.0f, 250.0f, 760.0f, 606.0f};
constexpr float kRowHeight = 78.0f;
constexpr Rect kModeRow{1022.0f, 738.0f, 736.0f, 94.0f};
constexpr Rect kDialog{550.0f, 180.0f, 820.0f, 720.0f};

// The game settings dialog's rows, and the window that shows five of them (also the Mods list's).
enum GameRow : int
{
    row_mode,
    row_renderer,
    row_resolution,
    row_filter,
    row_refresh,
    row_mods,
    row_save, // in builds that move saves
};
constexpr float kDialogRowsTop = 334.0f;
constexpr float kDialogRowPitch = 96.0f;
constexpr float kDialogRowHeight = 94.0f;
constexpr int kDialogRowsShown = 5;
constexpr Rect kDialogWindow{592.0f, kDialogRowsTop, 736.0f,
                             kDialogRowPitch * (kDialogRowsShown - 1) + kDialogRowHeight};
constexpr float kDialogHints = 848.0f;

// Console mode marks, drawn from lines: a screen on its stand, and a handheld.
void draw_docked(Canvas &c, float x, float cy, Color ink)
{
    c.list.bordered_rect({x, cy - 13.0f, 36.0f, 22.0f}, 3.0f, ink.with_alpha(0.0f), 2.0f, ink);
    c.list.line(x + 11.0f, cy + 14.0f, x + 25.0f, cy + 14.0f, 2.0f, ink);
}

void draw_handheld(Canvas &c, float x, float cy, Color ink)
{
    c.list.bordered_rect({x, cy - 10.0f, 40.0f, 20.0f}, 6.0f, ink.with_alpha(0.0f), 2.0f, ink);
    c.list.circle(x + 7.0f, cy, 2.2f, ink);
    c.list.circle(x + 33.0f, cy, 2.2f, ink);
}

} // namespace

void Launcher::start_scan()
{
    if (!scan_.valid() && home_.setup_ready)
        scan_ = std::async(std::launch::async, [this] { return services_.games(); });
}

void Launcher::finish_scan(bool wait)
{
    if (!scan_.valid())
        return;
    if (!wait && scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try
    {
        apply_games(scan_.get());
    }
    catch (const std::exception &error)
    {
        sys::log("game list: %s", error.what());
        games_loaded_ = true; // the list stays as it was
    }
}

void Launcher::apply_games(std::vector<Game> games)
{
    // The same games in the same order (the usual case) keep the list where it is; otherwise
    // the selection follows its game.
    bool same = games_loaded_ && games.size() == games_.size();
    for (std::size_t i = 0; same && i < games.size(); ++i)
        same = games[i].file == games_[i].file;
    const std::string selected = library_.selected < static_cast<int>(games_.size()) ?
                                     games_[static_cast<std::size_t>(library_.selected)].file :
                                     std::string{};
    games_ = std::move(games);
    games_loaded_ = true;
    for (Game &game : games_)
        if (game.title_id != 0)
            count_mods(game, services_.mods(game.title_id));
    if (!same)
    {
        int index = 0;
        for (int i = 0; i < static_cast<int>(games_.size()); ++i)
            if (games_[static_cast<std::size_t>(i)].file == selected)
                index = i;
        library_.reset(static_cast<int>(games_.size()), index);
        refresh_selected_game();
        mode_.snap(selected_docked_ ? 0.0f : 1.0f);
    }
    name_home_games();
}

void Launcher::name_home_games()
{
    // Games carry their own names; until one has been read the home screen names it by its file.
    for (const Game &game : games_)
    {
        if (game.file == home_.last_file)
            home_.last_title = game.name;
        for (Recent &recent : home_.recents)
            if (recent.file == game.file)
                recent.title = game.name;
    }
}

void Launcher::read_home()
{
    home_ = services_.home();
    if (home_.last_title_id == 0)
        return;
    const std::vector<Mod> mods = services_.mods(home_.last_title_id);
    home_.last_mods = static_cast<int>(mods.size());
    home_.last_mods_on = static_cast<int>(
        std::count_if(mods.begin(), mods.end(), [](const Mod &mod) { return mod.enabled; }));
}

void Launcher::count_mods(Game &game, const std::vector<Mod> &mods)
{
    game.mods = static_cast<int>(mods.size());
    game.mods_on = static_cast<int>(
        std::count_if(mods.begin(), mods.end(), [](const Mod &mod) { return mod.enabled; }));
    // The home screen says the same of its game.
    if (game.file == home_.last_file && home_.last_title_id != 0)
    {
        home_.last_mods = game.mods;
        home_.last_mods_on = game.mods_on;
    }
}

std::string Launcher::addons_line(const std::string &addons, int mods, int mods_on, bool brief)
{
    std::string line = addons;
    if (mods > 0)
    {
        // Mods that are switched off are still there: "1 of 2 mods on", or "1/2 mods" where there
        // is little room.
        const std::string count = std::to_string(mods);
        const char *counted = mods == 1 ? tr("{0} mod") : tr("{0} mods");
        line += (line.empty() ? "" : ", ") +
                (mods_on == mods ? fill(counted, {count}) :
                 brief ? fill(counted, {std::to_string(mods_on) + "/" + count}) :
                         fill(tr("{0} of {1} mods on"), {std::to_string(mods_on), count}));
    }
    return line.empty() ? std::string{tr("None")} : line;
}

std::string Launcher::hertz(int refresh)
{
    return fill(tr("{0} Hz"), {refresh == 1 ? "120" : "60"});
}

void Launcher::enter_library()
{
    if (games_loaded_)
    {
        // Games copied to the console since the list was read appear in a moment.
        finish_scan(false);
        start_scan();
    }
    else
    {
        start_scan();
        finish_scan(true);
    }
    library_.visible = 7;
    library_.pitch = 88.0f;
    library_.reset(static_cast<int>(games_.size()), 0);
    refresh_selected_game();
    detail_.snap(1.0f);
    mode_.snap(selected_docked_ ? 0.0f : 1.0f);
}

void Launcher::refresh_selected_game()
{
    const std::uint64_t id =
        games_.empty() ? 0 : games_[static_cast<std::size_t>(library_.selected)].title_id;
    selected_docked_ = id == 0 || services_.docked(id);
    detail_.value = 0.0f;
    detail_.velocity = 0.0f;
}

void Launcher::press_library(Key key)
{
    const int count = static_cast<int>(games_.size());
    const Game *game = count > 0 ? &games_[static_cast<std::size_t>(library_.selected)] : nullptr;
    switch (key)
    {
    case Key::circle:
        open(Screen::home, false);
        return;
    case Key::up:
    case Key::down:
        if (library_.move(key == Key::down ? 1 : -1))
        {
            message_.clear();
            refresh_selected_game();
            mode_.snap(selected_docked_ ? 0.0f : 1.0f);
            cue(Cue::focus);
        }
        return;
    case Key::l1:
    case Key::r1:
        if (library_.page(key == Key::r1 ? 1 : -1))
        {
            message_.clear();
            refresh_selected_game();
            mode_.snap(selected_docked_ ? 0.0f : 1.0f);
            cue(Cue::page);
        }
        return;
    case Key::left:
    case Key::right:
    {
        if (game == nullptr || game->title_id == 0)
        {
            if (game != nullptr)
                cue(Cue::error);
            return;
        }
        const bool saved = services_.set_docked(game->title_id, !selected_docked_);
        if (saved)
            selected_docked_ = !selected_docked_;
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save console mode. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    case Key::triangle:
        if (game == nullptr)
            return;
        if (game->title_id == 0)
        {
            say(tr("This game's settings cannot be saved (no title ID)."), true);
            cue(Cue::error);
            return;
        }
        game_settings_ = services_.game_settings(game->title_id);
        game_docked_ = selected_docked_;
        import_source_ = services_.save_transfer_available() ?
                             services_.save_import_source(game->title_id) : SaveSource::none;
        import_armed_ = false;
        mods_ = services_.mods(game->title_id);
        count_mods(games_[static_cast<std::size_t>(library_.selected)], mods_);
        open_modal(Modal::game);
        game_rows_.visible = kDialogRowsShown;
        game_rows_.pitch = kDialogRowPitch;
        game_rows_.reset(dialog_rows(Modal::game), 0);
        return;
    case Key::cross:
        if (game == nullptr || !home_.setup_ready)
        {
            cue(Cue::error);
            return;
        }
        launch(game->file, game->name, game->cover);
        return;
    default:
        return;
    }
}

void Launcher::draw_library(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const int count = static_cast<int>(games_.size());
    draw_frame(c, tr("Your games"), tr("Select a game to begin"));

    // ---- the list ----
    glass(c, kListPanel, 26.0f, theme::kPanel.with_alpha(0.80f), theme::kPanelEdge.with_alpha(0.55f));
    text(c, tr("LIBRARY"), 138.0f, baseline(208.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    if (count == 0)
    {
        text(c, tr("No ROM files found."), kListPanel.x + kListPanel.w * 0.5f,
             baseline(488.0f, 38.0f, theme::kText24), theme::kText24, theme::kCopy, Align::center);
    }
    else
    {
        // The window is wider than its rows so the highlight's glow is not cut at the sides.
        list.push_clip({kWindow.x - 24.0f, kWindow.y - 6.0f, kWindow.w + 48.0f, kWindow.h + 12.0f});
        const auto row_rect = [&](int row) -> Rect
        {
            return {kWindow.x,
                    kWindow.y + static_cast<float>(row) * library_.pitch - library_.scroll(),
                    kWindow.w, kRowHeight};
        };
        for (int row = library_.first_row(); row <= library_.last_row(); ++row)
        {
            list.push_opacity(library_.row_alpha(row, kRowHeight));
            plate_rest(c, kListPlate, row_rect(row));
            list.pop_opacity();
        }
        plate_focus(c, kListPlate,
                    {kWindow.x, kWindow.y + library_.cursor() - library_.scroll(), kWindow.w,
                     kRowHeight},
                    1.0f);
        for (int row = library_.first_row(); row <= library_.last_row(); ++row)
        {
            const Game &game = games_[static_cast<std::size_t>(row)];
            const Rect r = row_rect(row);
            list.push_opacity(library_.row_alpha(row, kRowHeight));
            cover(c, game.cover, {r.x + 12.0f, r.y + 11.0f, 56.0f, 56.0f}, 8.0f);
            text_fit(c, game.name, r.x + 86.0f, baseline(r.y, kRowHeight, theme::kText24),
                     theme::kText24, Color::rgb(0xf3f5e9), 560.0f);
            text(c, game.format, r.x + 730.0f, baseline(r.y, kRowHeight, theme::kSmall),
                 theme::kSmall, theme::kMeta, Align::right);
            list.pop_opacity();
        }
        list.pop_clip();
        scrollbar(c, library_, 910.0f, kWindow.y, kWindow.h);
    }
    text(c, list_position(count > 0 ? library_.selected + 1 : 0, count), 898.0f, baseline(866.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::right);

    // ---- the selected game ----
    glass(c, kDetailPanel, 26.0f, theme::kPanel.with_alpha(0.80f),
          theme::kPanelEdge.with_alpha(0.55f));
    text(c, tr("GAME DETAILS"), 1016.0f, baseline(210.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    const Game *game = count > 0 ? &games_[static_cast<std::size_t>(library_.selected)] : nullptr;
    const float shown = tween::clamp01(detail_.value);
    list.push_opacity(shown);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - shown) * 10.0f * motion());
    text_block(c, game != nullptr ? game->name : tr("No ROM selected"), 1016.0f,
               baseline(250.0f, 42.0f, theme::kHeading), theme::kHeading, 42.0f, theme::kTitle,
               760.0f, 2);
    cover(c, game != nullptr ? game->cover : std::string{}, {1016.0f, 372.0f, 288.0f, 288.0f},
          14.0f, 0.9f);
    if (game == nullptr || game->cover.empty())
        text_shrink(c, game == nullptr ? tr("Select a game") : tr("No cover art"), 1160.0f,
                    baseline(676.0f, 30.0f, theme::kSmall), theme::kSmall, theme::kMeta, 288.0f,
                    Align::center);
    struct Field
    {
        const char *label;
        std::string value;
        Color color;
    };
    Field fields[] = {
        {tr("FORMAT"), game != nullptr ? game->format : "-", theme::kValue},
        {tr("SIZE"), game != nullptr ? game->size : "-", theme::kValue},
        {tr("ADD-ONS"),
         game != nullptr ? addons_line(game->addons, game->mods, game->mods_on) : "-",
         game != nullptr && (!game->addons.empty() || game->mods > 0) ? theme::kLimePale :
                                                                         theme::kValue},
        {tr("LANGUAGE"), game != nullptr ? game->language : "-",
         game != nullptr && !game->language_note.empty() ? theme::kWarning : theme::kValue},
    };
    for (int i = 0; i < 4; ++i)
    {
        const float line = baseline(384.0f + 42.0f * static_cast<float>(i), 30.0f, theme::kSmall);
        // The name keeps its size up to 200 wide; the value has the rest of the line.
        const float label =
            text_shrink(c, fields[i].label, 1336.0f, line, theme::kSmall, theme::kLabel, 200.0f);
        const float room = 440.0f - label - 16.0f;
        // What a game comes with is said briefly where the whole line does not fit.
        if (i == 2 && game != nullptr && text_width(c, fields[i].value, theme::kSmall) > room)
            fields[i].value = addons_line(game->addons_short, game->mods, game->mods_on, true);
        text_shrink(c, fields[i].value, 1776.0f, line, theme::kSmall, fields[i].color, room,
                    Align::right);
    }
    if (game != nullptr && !game->language_note.empty())
        notice(c, game->language_note, 1776.0f, baseline(544.0f, 28.0f, 18.0f), 18.0f,
               theme::kWarning, 440.0f, true, Align::right);
    text(c, tr("FILE"), 1336.0f, baseline(582.0f, 30.0f, theme::kSmall), theme::kSmall, theme::kLabel);
    text_block(c, game != nullptr ? game->file : "-", 1336.0f,
               baseline(622.0f, 30.0f, theme::kSmall), theme::kSmall, 30.0f, theme::kValue, 440.0f,
               3);
    list.pop_transform();
    list.pop_opacity();

    // ---- console mode ----
    const bool can_configure = game != nullptr && game->title_id != 0;
    plate_rest(c, kRowPlate, kModeRow);
    text_shrink(c, tr("Console mode"), kModeRow.x + 26.0f,
                baseline(kModeRow.y, 94.0f, theme::kText24), theme::kText24,
                can_configure ? theme::kValue : theme::kMeta, 256.0f);
    if (can_configure)
    {
        const Rect track{1322.0f, 753.0f, 420.0f, 64.0f};
        const float half = track.w * 0.5f;
        list.rounded_rect(track, 14.0f, Color::rgb(0x0d1814, 0.75f));
        plate_focus(c, kNavPlate, {track.x + half * mode_.value + 3.0f, track.y + 3.0f, half - 6.0f,
                                   track.h - 6.0f},
                    1.0f);
        const float cy = track.y + track.h * 0.5f;
        const Color docked = gfx::mix(theme::kLimePale, theme::kValue.with_alpha(0.45f), mode_.value);
        const Color handheld =
            gfx::mix(theme::kValue.with_alpha(0.45f), theme::kLimePale, mode_.value);
        draw_docked(c, track.x + 26.0f, cy, docked);
        text_shrink(c, tr("Docked"), track.x + 78.0f, baseline(track.y, track.h, theme::kText24),
                    theme::kText24, docked, half - 78.0f - 10.0f, Align::left, 0.0f, 0.7f);
        draw_handheld(c, track.x + half + 18.0f, cy, handheld);
        text_shrink(c, tr("Handheld"), track.x + half + 72.0f,
                    baseline(track.y, track.h, theme::kText24), theme::kText24, handheld,
                    half - 72.0f - 10.0f, Align::left, 0.0f, 0.7f);
    }
    else
    {
        text_shrink(c, tr("Unavailable"), kModeRow.x + kModeRow.w - 26.0f,
                    baseline(kModeRow.y, 94.0f, theme::kText24), theme::kText24, theme::kMeta,
                    400.0f, Align::right);
    }
    draw_pad(c, Pad::leftright, 1022.0f, 865.0f, 26.0f);
    // A message said while Game settings is open belongs to that dialog.
    const bool said = !message_.empty() && modal_shown_ != Modal::game && modal_shown_ != Modal::mods;
    const std::string hint = said ? message_ :
                             can_configure ? tr("Change mode. Saved per game.") :
                                             tr("Select a readable game to configure its mode.");
    notice(c, hint, 1060.0f, 872.0f, theme::kSmall,
           said ? (message_warning_ ? theme::kWarning : theme::kLimePale) : theme::kMeta, 700.0f,
           said && message_warning_);

    static constexpr Hint kHints[] = {{Pad::cross, TR("Select")},
                                      {Pad::circle, TR("Back")},
                                      {Pad::updown, TR("Browse games")},
                                      {Pad::leftright, TR("Console mode")},
                                      {Pad::triangle, TR("Game settings")}};
    draw_footer(c, kHints, 5);
}

// ---------------------------------------------------------------- game settings dialog

void Launcher::press_game(Key key)
{
    Game &game = games_[static_cast<std::size_t>(library_.selected)];
    switch (key)
    {
    case Key::circle:
        close_modal();
        selected_docked_ = game_docked_;
        return;
    case Key::up:
    case Key::down:
        if (game_rows_.move(key == Key::down ? 1 : -1))
        {
            option_ = game_rows_.selected;
            message_.clear();
            import_armed_ = false;
            cue(Cue::focus);
        }
        return;
    case Key::square:
        if (option_ != row_save)
            return;
        break;
    case Key::left:
    case Key::right:
    case Key::cross:
        break;
    default:
        return;
    }
    if (option_ == row_mods)
    {
        // The game's mods have their own list. It is read again: mods may have been copied in
        // since this dialog opened.
        if (key != Key::cross)
            return;
        mods_ = services_.mods(game.title_id);
        count_mods(game, mods_);
        mod_rows_.visible = kDialogRowsShown;
        mod_rows_.pitch = kDialogRowPitch;
        mod_rows_.reset(static_cast<int>(mods_.size()), 0);
        modal_ = modal_shown_ = Modal::mods;
        message_.clear();
        cue(Cue::open);
        return;
    }
    if (option_ == row_save)
    {
        // Save data: Square copies the game's save out, Cross (twice) copies one in.
        std::string result;
        if (key == Key::square)
        {
            import_armed_ = false;
            const bool exported = services_.save_export(game.title_id, &result);
            say(result, !exported);
            cue(exported ? Cue::saved : Cue::error);
        }
        else if (key != Key::cross)
        {
            cue(Cue::error);
        }
        else if (import_source_ != SaveSource::none && !import_armed_)
        {
            // Importing replaces the save in use, so it asks first.
            import_armed_ = true;
            say(tr("Press again to replace this game's save. The current one is backed up."), true);
            cue(Cue::notify);
        }
        else
        {
            // With nothing to import the answer says where a save has to be put.
            import_armed_ = false;
            const bool imported = services_.save_import(game.title_id, &result);
            say(result, !imported && import_source_ != SaveSource::none);
            cue(imported ? Cue::saved : import_source_ != SaveSource::none ? Cue::error : Cue::notify);
        }
        return;
    }
    const int step = key == Key::left ? -1 : 1;
    bool saved = false;
    if (option_ == row_mode)
    {
        saved = services_.set_docked(game.title_id, !game_docked_);
        if (saved)
            game_docked_ = !game_docked_;
    }
    else
    {
        // Default (-1), then each value.
        const auto cycle = [step](int value, int count)
        { return (value + 1 + step + count + 1) % (count + 1) - 1; };
        GameSettings next = game_settings_;
        if (option_ == row_renderer)
            next.renderer = cycle(next.renderer, 2);
        if (option_ == row_resolution)
            next.resolution =
                cycle(next.resolution, static_cast<int>(services_.resolution_labels().size()));
        if (option_ == row_filter)
            next.filter = cycle(next.filter, static_cast<int>(services_.filter_labels().size()));
        if (option_ == row_refresh)
            next.refresh = cycle(next.refresh, 2);
        saved = services_.set_game_settings(game.title_id, next);
        if (saved)
            game_settings_ = next;
    }
    // 120 Hz is a request: the display has the last word.
    const bool fast = saved && option_ == row_refresh &&
                      (game_settings_.refresh >= 0 ? game_settings_.refresh : prefs_.refresh) == 1;
    say(fast ? tr("Saved. A display that cannot show 120 Hz stays at 60 Hz.") :
        saved ? tr("Saved for this game. Applies on next launch.") : tr("Could not save. Please try again."),
        !saved);
    cue(saved ? Cue::toggle : Cue::error);
}

void Launcher::draw_game(Canvas &c, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.03f * (1.0f - open) * motion(), 960.0f, 540.0f, 0.0f,
                        (1.0f - open) * 26.0f * motion());
    glass(c, kDialog, 26.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);
    text_shrink(c, tr("Game settings"), 592.0f, baseline(218.0f, 62.0f, theme::kDisplay),
                theme::kDisplay, theme::kTitle, 736.0f);
    const Game *game = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    text_fit(c, game != nullptr ? game->name : std::string{}, 592.0f,
             baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall, Color::rgb(0xbecbb9), 736.0f);

    static constexpr const char *kRenderers[] = {"OpenGL", "Vulkan"};
    const auto &resolutions = services_.resolution_labels();
    // A resolution's short name is how its label starts: "0.5x (faster, softer)" is "0.5x".
    const auto short_resolution = [](const std::string &label)
    { return label.substr(0, label.find(' ')); };
    const auto &filters = services_.filter_labels();
    const auto pick = [](const std::vector<std::string> &values, int index) -> std::string
    {
        return index >= 0 && index < static_cast<int>(values.size()) ?
                   values[static_cast<std::size_t>(index)] : std::string{"-"};
    };
    const std::string values[] = {
        game_docked_ ? tr("Docked") : tr("Handheld"),
        game_settings_.renderer >= 0 ? kRenderers[game_settings_.renderer] :
            fill(tr("Default ({0})"), {kRenderers[prefs_.renderer != 0 ? 1 : 0]}),
        game_settings_.resolution >= 0 ? pick(resolutions, game_settings_.resolution) :
            fill(tr("Default ({0})"), {short_resolution(pick(resolutions, prefs_.resolution))}),
        game_settings_.filter >= 0 ? pick(filters, game_settings_.filter) :
            fill(tr("Default ({0})"), {pick(filters, prefs_.filter)}),
        game_settings_.refresh >= 0 ? hertz(game_settings_.refresh) :
            fill(tr("Default ({0})"), {hertz(prefs_.refresh)}),
        mods_.empty() ? std::string{tr("No mods")} :
            fill(tr("{0} of {1} on"),
                 {std::to_string(std::count_if(mods_.begin(), mods_.end(),
                                               [](const Mod &mod) { return mod.enabled; })),
                  std::to_string(mods_.size())}),
        import_source_ == SaveSource::ryujinx ? tr("Ryujinx save found") :
        import_source_ == SaveSource::folder ? tr("Save folder found") : tr("Nothing to import"),
    };
    static constexpr const char *kLabels[] = {TR("Console mode"), TR("Renderer"), TR("Resolution"),
                                              TR("Upscaling filter"), TR("Refresh rate"),
                                              TR("Mods"), TR("Save data")};
    // Five rows show; the list scrolls to the others.
    list.push_clip({kDialogWindow.x - 24.0f, kDialogWindow.y - 6.0f, kDialogWindow.w + 48.0f,
                    kDialogWindow.h + 12.0f});
    const auto row_top = [&](int row)
    { return kDialogRowsTop + static_cast<float>(row) * kDialogRowPitch - game_rows_.scroll(); };
    for (int row = game_rows_.first_row(); row <= game_rows_.last_row(); ++row)
    {
        list.push_opacity(game_rows_.row_alpha(row, kDialogRowHeight));
        plate_rest(c, kRowPlate, {592.0f, row_top(row), 736.0f, kDialogRowHeight});
        list.pop_opacity();
    }
    plate_focus(c, kRowPlate,
                {592.0f, kDialogRowsTop + game_rows_.cursor() - game_rows_.scroll(), 736.0f,
                 kDialogRowHeight},
                1.0f);
    for (int row = game_rows_.first_row(); row <= game_rows_.last_row(); ++row)
    {
        const float top = row_top(row);
        const float focus = row == option_ ? 1.0f : 0.0f;
        list.push_opacity(game_rows_.row_alpha(row, kDialogRowHeight));
        // The value first: the row's name takes what it leaves. Mods and Save data say what is
        // there; the others are choices.
        const bool there = row == row_mods ? !mods_.empty() : import_source_ != SaveSource::none;
        const float taken =
            row >= row_mods ?
                text_shrink(c, values[row], 1292.0f, baseline(top, kDialogRowHeight, theme::kSmall),
                            theme::kSmall, there ? theme::kLimePale : theme::kMeta, 320.0f,
                            Align::right) :
                chooser(c, values[row], 1296.0f, baseline(top, kDialogRowHeight, theme::kText24),
                        focus, theme::kLimePale);
        text_shrink(c, tr(kLabels[row]), 628.0f, baseline(top, kDialogRowHeight, theme::kText24),
                    theme::kText24, theme::kValue, 664.0f - taken - 28.0f);
        list.pop_opacity();
    }
    list.pop_clip();
    scrollbar(c, game_rows_, 1340.0f, kDialogWindow.y, kDialogWindow.h);

    if (!message_.empty())
    {
        // Up to two lines: what Save data answers is longer than a "Saved".
        notice_block(c, message_, 592.0f, kDialogHints - 6.0f, theme::kSmall, 26.0f,
                     message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f, 2,
                     message_warning_);
    }
    else if (option_ == row_save)
    {
        static constexpr Hint kTransfer[] = {{Pad::cross, TR("Import")},
                                             {Pad::square, TR("Export a copy")},
                                             {Pad::circle, TR("Back")}};
        draw_hints(c, kTransfer, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else if (option_ == row_mods)
    {
        static constexpr Hint kOpen[] = {{Pad::cross, TR("Open")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kOpen, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {
            {Pad::updown, TR("Select")}, {Pad::leftright, TR("Change")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

// ---------------------------------------------------------------- a game's mods

void Launcher::press_mods(Key key)
{
    Game &game = games_[static_cast<std::size_t>(library_.selected)];
    switch (key)
    {
    case Key::circle:
        // Back to the game's settings, on its Mods row.
        modal_ = modal_shown_ = Modal::game;
        message_.clear();
        cue(Cue::back);
        return;
    case Key::up:
    case Key::down:
        if (mod_rows_.move(key == Key::down ? 1 : -1))
        {
            message_.clear();
            cue(Cue::focus);
        }
        return;
    case Key::square:
    {
        // With no mods yet: the folder they go in, named after the game's ID, is made on request.
        if (!mods_.empty())
            return;
        const bool made = services_.make_mods_folder(game.title_id);
        say(made ? fill(tr("Created {0}. Copy each mod's folder into it."),
                        {services_.mods_folder(game.title_id)}) :
                   tr("Could not create the folder. Check that the game files folder can be written."),
            !made);
        cue(made ? Cue::saved : Cue::error);
        return;
    }
    case Key::cross:
    case Key::left:
    case Key::right:
    {
        if (mods_.empty())
            return;
        Mod &mod = mods_[static_cast<std::size_t>(mod_rows_.selected)];
        const bool saved = services_.set_mod_enabled(game.title_id, mod.name, !mod.enabled);
        if (saved)
        {
            mod.enabled = !mod.enabled;
            count_mods(game, mods_);
        }
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    default:
        return;
    }
}

void Launcher::draw_mods(Canvas &c, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.03f * (1.0f - open) * motion(), 960.0f, 540.0f, 0.0f,
                        (1.0f - open) * 26.0f * motion());
    glass(c, kDialog, 26.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);
    text_shrink(c, tr("Mods"), 592.0f, baseline(218.0f, 62.0f, theme::kDisplay), theme::kDisplay,
                theme::kTitle, 736.0f);
    const Game *game = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    text_fit(c, game != nullptr ? game->name : std::string{}, 592.0f,
             baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall, Color::rgb(0xbecbb9), 736.0f);

    if (mods_.empty())
    {
        text_block(c,
                   fill(tr("No mods for this game yet. Copy each mod's folder to {0}, next to roms/."),
                        {game != nullptr ? services_.mods_folder(game->title_id) : std::string{}}),
                   592.0f, baseline(364.0f, 40.0f, theme::kText24), theme::kText24, 40.0f,
                   theme::kBody, 736.0f, 5, kShrink);
    }
    else
    {
        list.push_clip({kDialogWindow.x - 24.0f, kDialogWindow.y - 6.0f, kDialogWindow.w + 48.0f,
                        kDialogWindow.h + 12.0f});
        const auto row_top = [&](int row)
        { return kDialogRowsTop + static_cast<float>(row) * kDialogRowPitch - mod_rows_.scroll(); };
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            plate_rest(c, kRowPlate, {592.0f, row_top(row), 736.0f, kDialogRowHeight});
            list.pop_opacity();
        }
        plate_focus(c, kRowPlate,
                    {592.0f, kDialogRowsTop + mod_rows_.cursor() - mod_rows_.scroll(), 736.0f,
                     kDialogRowHeight},
                    1.0f);
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            const Mod &mod = mods_[static_cast<std::size_t>(row)];
            const float top = row_top(row);
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            // Its name as the player's folder has it, what it changes under it, its switch.
            text_fit(c, mod.name, 628.0f, baseline(top + 14.0f, 38.0f, theme::kText24),
                     theme::kText24, mod.enabled ? theme::kValue : theme::kMeta, 560.0f);
            text_shrink(c, mod.kind, 628.0f, baseline(top + 52.0f, 28.0f, theme::kSmall),
                        theme::kSmall, theme::kMeta, 560.0f);
            toggle(c, 1292.0f, top + kDialogRowHeight * 0.5f, mod.enabled ? 1.0f : 0.0f);
            list.pop_opacity();
        }
        list.pop_clip();
        scrollbar(c, mod_rows_, 1340.0f, kDialogWindow.y, kDialogWindow.h);
    }

    if (!message_.empty())
    {
        notice_block(c, message_, 592.0f, kDialogHints - 6.0f, theme::kSmall, 26.0f,
                     message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f, 2,
                     message_warning_);
    }
    else if (mods_.empty())
    {
        static constexpr Hint kEmpty[] = {{Pad::square, TR("Create the folder")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kEmpty, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {{Pad::updown, TR("Select")},
                                          {Pad::cross, TR("Turn on or off")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

} // namespace pe::ui
