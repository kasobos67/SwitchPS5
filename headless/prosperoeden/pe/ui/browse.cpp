// ProsperoEden - Launcher: the Game files folder browser, Language and About.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace pe::ui
{

using audio::Cue;

namespace
{

constexpr Rect kListPanel{108.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kDetailPanel{980.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kWindow{138.0f, 292.0f, 760.0f, 518.0f};
constexpr float kRowHeight = 78.0f;

std::string join_path(const std::string &directory, const std::string &name)
{
    return directory == "/" ? "/" + name : directory + "/" + name;
}

std::string parent_path(const std::string &directory)
{
    const std::size_t slash = directory.find_last_of('/');
    return slash == 0 || slash == std::string::npos ? "/" : directory.substr(0, slash);
}

std::string short_path(const std::string &path, std::size_t limit)
{
    return path.size() <= limit ? path : "..." + path.substr(path.size() - (limit - 3));
}

std::string plural(int count, const char *one, const char *many)
{
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

// A folder mark from two rounded shapes; `up` adds the arrow of the parent folder.
void draw_folder(Canvas &c, float x, float cy, Color ink, bool up)
{
    c.list.rounded_rect({x, cy - 13.0f, 18.0f, 8.0f}, 3.0f, ink);
    c.list.rounded_rect({x, cy - 9.0f, 40.0f, 24.0f}, 4.0f, ink);
    if (up)
    {
        const Color dark = Color::rgb(0x12201a);
        c.list.line(x + 20.0f, cy + 9.0f, x + 20.0f, cy - 3.0f, 2.4f, dark);
        c.list.line(x + 14.0f, cy + 2.0f, x + 20.0f, cy - 4.0f, 2.4f, dark);
        c.list.line(x + 26.0f, cy + 2.0f, x + 20.0f, cy - 4.0f, 2.4f, dark);
    }
}

} // namespace

// ---------------------------------------------------------------- game files

void Launcher::enter_files()
{
    // Start where the player's files are (or will be after reopening), else the nearest
    // folder that can be opened.
    std::string start = services_.saved_files_folder();
    if (start.empty())
        start = services_.files_folder();
    files_.visible = 6;
    files_.pitch = 88.0f;
    while (!browse_to(start) && start != "/")
        start = parent_path(start);
    message_.clear();
}

bool Launcher::browse_to(const std::string &directory)
{
    std::vector<std::string> folders;
    if (!services_.folders(directory, &folders))
        return false;
    browse_dir_ = directory;
    browse_entries_.clear();
    if (directory != "/")
        browse_entries_.push_back("..");
    browse_entries_.insert(browse_entries_.end(), folders.begin(), folders.end());
    files_.reset(static_cast<int>(browse_entries_.size()), 0);
    // The folder shown is what TRIANGLE saves: show what it holds.
    folder_info_ = services_.folder_info(directory);
    return true;
}

void Launcher::press_files(Key key)
{
    const int count = static_cast<int>(browse_entries_.size());
    switch (key)
    {
    case Key::circle:
        open(Screen::settings, false);
        return;
    case Key::up:
    case Key::down:
        if (files_.move(key == Key::down ? 1 : -1))
            cue(Cue::focus);
        return;
    case Key::l1:
    case Key::r1:
        if (files_.page(key == Key::r1 ? 1 : -1))
            cue(Cue::page);
        return;
    case Key::cross:
    {
        if (count == 0)
            return;
        const std::string entry = browse_entries_[static_cast<std::size_t>(files_.selected)];
        const std::string from = browse_dir_;
        const bool up = entry == "..";
        if (!browse_to(up ? parent_path(browse_dir_) : join_path(browse_dir_, entry)))
        {
            say("This folder cannot be opened.", true);
            cue(Cue::error);
            return;
        }
        message_.clear();
        // Going up keeps the folder just left in view.
        if (up)
        {
            const std::string left = from.substr(from.find_last_of('/') + 1);
            for (int i = 0; i < static_cast<int>(browse_entries_.size()); ++i)
                if (browse_entries_[static_cast<std::size_t>(i)] == left)
                {
                    files_.reset(files_.count, i);
                    break;
                }
        }
        cue(Cue::select);
        return;
    }
    case Key::triangle:
    {
        const bool saved = services_.set_files_folder(browse_dir_);
        say(saved ? "Saved. Reopen ProsperoEden to use this folder." :
                    "Could not save the folder. Please try again.",
            !saved);
        cue(saved ? Cue::saved : Cue::error);
        return;
    }
    case Key::square:
    {
        const std::string fallback = services_.default_files_folder();
        const bool saved = services_.set_files_folder(fallback);
        (void)browse_to(fallback);
        say(saved ? "Default folder saved. Reopen ProsperoEden to use it." :
                    "Could not save the folder. Please try again.",
            !saved);
        cue(saved ? Cue::saved : Cue::error);
        return;
    }
    default:
        return;
    }
}

void Launcher::draw_files(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const int count = static_cast<int>(browse_entries_.size());
    draw_frame(c, "Game files", "Choose the folder that holds your keys, firmware and games");

    // ---- folders ----
    glass(c, kListPanel, 26.0f, theme::kPanel.with_alpha(0.80f), theme::kPanelEdge.with_alpha(0.55f));
    text(c, "FOLDERS", 138.0f, baseline(208.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    text(c, short_path(browse_dir_, 52), 138.0f, baseline(246.0f, 28.0f, theme::kSmall),
         theme::kSmall, theme::kBody);
    if (count == 0)
    {
        text(c, "No folders here.", kListPanel.x + kListPanel.w * 0.5f,
             baseline(488.0f, 38.0f, theme::kText24), theme::kText24, theme::kCopy, Align::center);
    }
    else
    {
        list.push_clip({kWindow.x - 24.0f, kWindow.y - 6.0f, kWindow.w + 48.0f, kWindow.h + 12.0f});
        const auto row_rect = [&](int row) -> Rect
        {
            return {kWindow.x, kWindow.y + static_cast<float>(row) * files_.pitch - files_.scroll(),
                    kWindow.w, kRowHeight};
        };
        for (int row = files_.first_row(); row <= files_.last_row(); ++row)
        {
            list.push_opacity(files_.row_alpha(row, kRowHeight));
            plate_rest(c, kListPlate, row_rect(row));
            list.pop_opacity();
        }
        plate_focus(c, kListPlate,
                    {kWindow.x, kWindow.y + files_.cursor() - files_.scroll(), kWindow.w, kRowHeight},
                    1.0f);
        for (int row = files_.first_row(); row <= files_.last_row(); ++row)
        {
            const std::string &entry = browse_entries_[static_cast<std::size_t>(row)];
            const bool up = entry == "..";
            const Rect r = row_rect(row);
            list.push_opacity(files_.row_alpha(row, kRowHeight));
            draw_folder(c, r.x + 24.0f, r.y + kRowHeight * 0.5f,
                        up ? theme::kLimePale : Color::rgb(0xc9d6bd), up);
            text_fit(c, up ? "Parent folder" : entry, r.x + 84.0f,
                     baseline(r.y, kRowHeight, theme::kText24), theme::kText24,
                     up ? theme::kLimePale : Color::rgb(0xf3f5e9), 552.0f);
            text(c, up ? "UP" : "OPEN", r.x + 730.0f, baseline(r.y, kRowHeight, theme::kSmall),
                 theme::kSmall, theme::kMeta, Align::right);
            list.pop_opacity();
        }
        list.pop_clip();
        scrollbar(c, files_, 910.0f, kWindow.y, kWindow.h);
    }
    char position[32];
    std::snprintf(position, sizeof(position), "%d OF %d", count > 0 ? files_.selected + 1 : 0,
                  count);
    text(c, position, 898.0f, baseline(866.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::right);

    // ---- what the folder shown holds ----
    glass(c, kDetailPanel, 26.0f, theme::kPanel.with_alpha(0.80f),
          theme::kPanelEdge.with_alpha(0.55f));
    text(c, "THIS FOLDER", 1016.0f, baseline(210.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    text_block(c, browse_dir_, 1016.0f, baseline(250.0f, 42.0f, theme::kHeading), theme::kHeading,
               42.0f, theme::kTitle, 748.0f, 2);
    const auto line = [&](float top, const char *label, const std::string &value, Color color)
    {
        text(c, label, 1016.0f, baseline(top, 30.0f, theme::kSmall), theme::kSmall, theme::kLabel,
             Align::left, 2.0f);
        text_fit(c, value, 1216.0f, baseline(top, 30.0f, theme::kSmall), theme::kSmall, color,
                 548.0f);
    };
    const auto state = [](bool ready) { return ready ? theme::kLimePale : theme::kWarning; };
    line(378.0f, "KEYS", folder_info_.keys ? "prod.keys found" : "prod.keys missing",
         state(folder_info_.keys));
    line(434.0f, "FIRMWARE",
         folder_info_.firmware < 0 ? "No firmware folder" :
                                     plural(folder_info_.firmware, "NCA file", "NCA files"),
         state(folder_info_.firmware > 0));
    line(490.0f, "GAMES",
         folder_info_.games < 0 ? "No roms folder" : plural(folder_info_.games, "game", "games"),
         state(folder_info_.games > 0));
    list.rounded_rect({1016.0f, 554.0f, 748.0f, 1.0f}, 0.0f, theme::kRule);
    const std::string in_use_folder = services_.files_folder();
    const std::string saved = services_.saved_files_folder();
    line(578.0f, "IN USE",
         !saved.empty() && saved != in_use_folder ? "Next launch: " + short_path(saved, 32) :
                                                    short_path(in_use_folder, 40),
         theme::kValue);
    const int access = services_.filesystem_access();
    line(634.0f, "ACCESS",
         access == 0 ? "Full filesystem" :
                       "Sandboxed (code " + std::to_string(access) + "): app folder only",
         state(access == 0));
    list.rounded_rect({1016.0f, 698.0f, 748.0f, 1.0f}, 0.0f, theme::kRule);
    text_block(c,
               message_.empty() ?
                   "Keep keys, firmware and roms folders together. TRIANGLE uses the folder shown." :
                   message_,
               1016.0f, baseline(722.0f, 36.0f, theme::kText24), theme::kText24, 36.0f,
               message_.empty() ? theme::kCopy :
               message_warning_ ? theme::kWarning : theme::kLimePale,
               748.0f, 3);

    static constexpr Hint kHints[] = {{Pad::cross, "Open"},
                                      {Pad::circle, "Back"},
                                      {Pad::triangle, "Use this folder"},
                                      {Pad::square, "Default folder"},
                                      {Pad::updown, "Browse"},
                                      {Pad::l1, "Page", Pad::r1}};
    draw_footer(c, kHints, 6);
}

// ---------------------------------------------------------------- language

void Launcher::enter_language()
{
    language_.visible = 6;
    language_.pitch = 88.0f;
    language_.reset(static_cast<int>(services_.language_labels().size()), prefs_.language);
    message_.clear();
}

void Launcher::press_language(Key key)
{
    switch (key)
    {
    case Key::circle:
        open(Screen::settings, false);
        // The home screen names the language the last game will use, and so does the Library.
        home_ = services_.home();
        name_home_games();
        finish_scan(false);
        start_scan();
        return;
    case Key::up:
    case Key::down:
        if (language_.move(key == Key::down ? 1 : -1))
        {
            message_.clear();
            cue(Cue::focus);
        }
        return;
    case Key::l1:
    case Key::r1:
        if (language_.page(key == Key::r1 ? 1 : -1))
        {
            message_.clear();
            cue(Cue::page);
        }
        return;
    case Key::cross:
    {
        const int before = prefs_.language;
        prefs_.language = language_.selected;
        const bool saved = services_.set_preferences(prefs_);
        if (!saved)
            prefs_.language = before;
        say(saved ? "Saved. Applies when a game starts." : "Could not save. Please try again.",
            !saved);
        cue(saved ? Cue::saved : Cue::error);
        return;
    }
    default:
        return;
    }
}

void Launcher::draw_language(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const auto &labels = services_.language_labels();
    const int count = static_cast<int>(labels.size());
    draw_frame(c, "Language", "Choose the language games use");

    glass(c, kListPanel, 26.0f, theme::kPanel.with_alpha(0.80f), theme::kPanelEdge.with_alpha(0.55f));
    text(c, "LANGUAGES", 138.0f, baseline(208.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    list.push_clip({kWindow.x - 24.0f, kWindow.y - 6.0f, kWindow.w + 48.0f, kWindow.h + 12.0f});
    const auto row_rect = [&](int row) -> Rect
    {
        return {kWindow.x, kWindow.y + static_cast<float>(row) * language_.pitch - language_.scroll(),
                kWindow.w, kRowHeight};
    };
    for (int row = language_.first_row(); row <= language_.last_row(); ++row)
    {
        list.push_opacity(language_.row_alpha(row, kRowHeight));
        plate_rest(c, kListPlate, row_rect(row));
        list.pop_opacity();
    }
    plate_focus(c, kListPlate,
                {kWindow.x, kWindow.y + language_.cursor() - language_.scroll(), kWindow.w,
                 kRowHeight},
                1.0f);
    for (int row = language_.first_row(); row <= language_.last_row(); ++row)
    {
        const Rect r = row_rect(row);
        list.push_opacity(language_.row_alpha(row, kRowHeight));
        text_fit(c, labels[static_cast<std::size_t>(row)], r.x + 26.0f,
                 baseline(r.y, kRowHeight, theme::kText24), theme::kText24, Color::rgb(0xf3f5e9),
                 520.0f);
        if (row == prefs_.language)
        {
            // The language in use carries a lime tag.
            const float width = c.fonts.font->measure("IN USE", 18.0f, 2.0f) + 28.0f;
            const Rect tag{r.x + 734.0f - width, r.y + 24.0f, width, 30.0f};
            list.bordered_rect(tag, 15.0f, theme::kLime.with_alpha(0.16f), 1.0f,
                               theme::kLime.with_alpha(0.55f));
            text(c, "IN USE", tag.x + tag.w * 0.5f, baseline(tag.y, 30.0f, 18.0f), 18.0f,
                 theme::kLimePale, Align::center, 2.0f);
        }
        list.pop_opacity();
    }
    list.pop_clip();
    scrollbar(c, language_, 910.0f, kWindow.y, kWindow.h);
    char position[32];
    std::snprintf(position, sizeof(position), "%d OF %d", language_.selected + 1, count);
    text(c, position, 898.0f, baseline(866.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::right);

    glass(c, kDetailPanel, 26.0f, theme::kPanel.with_alpha(0.80f),
          theme::kPanelEdge.with_alpha(0.55f));
    text(c, "SELECTED", 1016.0f, baseline(210.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    const auto label_at = [&](int index) -> std::string
    {
        return index >= 0 && index < count ? labels[static_cast<std::size_t>(index)] : std::string{};
    };
    text_fit(c, label_at(language_.selected), 1016.0f, baseline(250.0f, 42.0f, theme::kHeading),
             theme::kHeading, theme::kTitle, 748.0f);
    const auto line = [&](float top, const char *label, const std::string &value, Color color)
    {
        text(c, label, 1016.0f, baseline(top, 30.0f, theme::kSmall), theme::kSmall, theme::kLabel,
             Align::left, 2.0f);
        text_fit(c, value, 1216.0f, baseline(top, 30.0f, theme::kSmall), theme::kSmall, color,
                 548.0f);
    };
    line(378.0f, "REGION", services_.language_region(language_.selected), theme::kLimePale);
    line(434.0f, "IN USE", label_at(prefs_.language), theme::kValue);
    list.rounded_rect({1016.0f, 554.0f, 748.0f, 1.0f}, 0.0f, theme::kRule);
    text_block(c,
               "Games use this language when they offer it, and their own default otherwise. It "
               "applies when a game starts.",
               1016.0f, baseline(578.0f, 34.0f, 22.0f), 22.0f, 34.0f, theme::kCopy, 748.0f, 3);
    if (!message_.empty())
        text_fit(c, message_, 1016.0f, baseline(722.0f, 34.0f, 22.0f), 22.0f,
                 message_warning_ ? theme::kWarning : theme::kLimePale, 748.0f);

    static constexpr Hint kHints[] = {{Pad::cross, "Choose"},
                                      {Pad::circle, "Back"},
                                      {Pad::updown, "Browse"},
                                      {Pad::l1, "Page", Pad::r1}};
    draw_footer(c, kHints, 4);
}

// ---------------------------------------------------------------- about

void Launcher::draw_about(Canvas &c)
{
    gfx::DrawList &list = c.list;
    draw_frame(c, "About ProsperoEden", "Credits and setup");

    glass(c, kListPanel, 26.0f, theme::kPanel.with_alpha(0.80f), theme::kPanelEdge.with_alpha(0.55f));
    text(c, "PROJECT CREDITS", 144.0f, baseline(210.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    text(c, "Powered by Eden", 144.0f, baseline(250.0f, 54.0f, theme::kLead), theme::kLead,
         theme::kTitle);
    text_block(c, "All credit for the Eden emulator goes to its developers and contributors.",
               144.0f, baseline(310.0f, 36.0f, theme::kText24), theme::kText24, 36.0f, theme::kBody,
               748.0f, 2);
    text(c, "eden-emu.dev", 144.0f, baseline(384.0f, 36.0f, theme::kText24), theme::kText24,
         theme::kLimePale);
    list.rounded_rect({144.0f, 444.0f, 748.0f, 1.0f}, 0.0f, theme::kRule);
    text(c, "THANKS", 144.0f, baseline(464.0f, 30.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 2.0f);
    text_block(c,
               "Thanks to the whole PS5 homebrew community and to every developer whose drivers, "
               "tools and libraries make ProsperoEden possible.",
               144.0f, baseline(500.0f, 36.0f, theme::kText24), theme::kText24, 36.0f, theme::kBody,
               748.0f, 3);
    list.rounded_rect({144.0f, 628.0f, 748.0f, 1.0f}, 0.0f, theme::kRule);
    text(c, "PS5 EDITION", 144.0f, baseline(648.0f, 30.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 2.0f);
    text_block(c, "ProsperoEden is an unofficial PS5 port brought to you by BlackBearReloaded.",
               144.0f, baseline(684.0f, 36.0f, theme::kText24), theme::kText24, 36.0f, theme::kBody,
               748.0f, 2);
    text(c, "Menu sound effects made with ElevenLabs.", 144.0f,
         baseline(788.0f, 30.0f, theme::kSmall), theme::kSmall, theme::kMeta);
    text(c, version_, 892.0f, baseline(866.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::right);

    glass(c, kDetailPanel, 26.0f, theme::kPanel.with_alpha(0.80f),
          theme::kPanelEdge.with_alpha(0.55f));
    text(c, "GETTING STARTED", 1016.0f, baseline(210.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    text(c, "Supply your own files", 1016.0f, baseline(262.0f, 54.0f, theme::kLead), theme::kLead,
         theme::kTitle);
    const std::string folder = services_.files_folder();
    const auto line = [&](float top, const char *label, const std::string &value)
    {
        text(c, label, 1016.0f, baseline(top, 30.0f, theme::kSmall), theme::kSmall, theme::kMeta);
        text_fit(c, value, 1232.0f, baseline(top, 34.0f, theme::kText24), theme::kText24,
                 theme::kValue, 532.0f);
    };
    line(384.0f, "KEYS", short_path(folder + "/keys/prod.keys", 36));
    line(464.0f, "FIRMWARE", short_path(folder + "/firmware/*.nca", 36));
    line(544.0f, "GAMES", short_path(folder + "/roms", 36) + "/ (NSP or XCI)");
    line(624.0f, "UPDATES, DLC", short_path(folder + "/updates", 36) + "/ (NSP or XCI)");
    list.rounded_rect({1016.0f, 712.0f, 748.0f, 1.0f}, 0.0f, theme::kRule);
    text_block(c,
               "Use extracted firmware NCA files. Choose the folder in Settings, Game files; "
               "restart the app after changing it.",
               1016.0f, baseline(740.0f, 36.0f, theme::kText24), theme::kText24, 36.0f,
               theme::kMeta, 748.0f, 3);

    static constexpr Hint kHints[] = {{Pad::circle, "Back"}};
    draw_footer(c, kHints, 1);
}

} // namespace pe::ui
