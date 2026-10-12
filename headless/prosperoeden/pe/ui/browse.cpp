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

std::string join_path(const std::string &directory, const std::string &name)
{
    return directory == "/" ? "/" + name : directory + "/" + name;
}

std::string parent_path(const std::string &directory)
{
    const std::size_t slash = directory.find_last_of('/');
    return slash == 0 || slash == std::string::npos ? "/" : directory.substr(0, slash);
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
        // Folder navigation stays on this screen, so leaving_ retains its
        // entry screen: HOME shortcut or the System Settings category.
        open(leaving_ == Screen::home ? Screen::home : Screen::settings, false);
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
            say(tr("This folder cannot be opened."), true);
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
        say(saved ? tr("Saved. Reopen Switch to use this folder.") :
                    tr("Could not save the folder. Please try again."),
            !saved);
        cue(saved ? Cue::saved : Cue::error);
        return;
    }
    case Key::square:
    {
        const std::string fallback = services_.default_files_folder();
        const bool saved = services_.set_files_folder(fallback);
        (void)browse_to(fallback);
        say(saved ? tr("Default folder saved. Reopen Switch to use it.") :
                    tr("Could not save the folder. Please try again."),
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
auto &l=c.list;
    draw_frame(c,tr("Data Management"),"");
    text_fit(c,browse_dir_,112,baseline(195,50,26),26,theme::kText,1670);
    l.line(784,269,784,916,1,theme::kRule.with_alpha(0.6f));
    const int count=static_cast<int>(browse_entries_.size());
    l.push_clip({108,280,644,536});
    const auto top=[&](int row){return 282.0f+files_.pitch*row-files_.scroll();};
    for(int row=files_.first_row();row<=files_.last_row();++row) {
        Rect r{112,top(row),628,78};
        plate(c,kRowPlate,r,row==files_.selected ? 1.0f : 0.0f);
        const std::string &name=browse_entries_[static_cast<std::size_t>(row)];
        draw_folder(c,134,r.y+39,theme::kMeta,name=="..");
        text_fit(c,name,202,baseline(r.y,78,24),24,theme::kText,510);
    }
    l.pop_clip();
    if(count==0) text_shrink(c,tr("No folders here."),132,500,24,theme::kCopy,580);
    scrollbar(c,files_,758,282,518);
    
    text_shrink(c,tr("Game Files"),844,baseline(290,52,30),30,theme::kTitle,950);
    const std::string labels[]={tr("Keys"),tr("Firmware"),tr("Software"),tr("Current Folder")};
    const std::string values[]={folder_info_.keys ? tr("Ready") : tr("Missing"),
        folder_info_.firmware>=0 ? std::to_string(folder_info_.firmware) : tr("Missing"),
        folder_info_.games>=0 ? std::to_string(folder_info_.games) : tr("Missing"),services_.files_folder()};
    for(int i=0;i<4;++i) {
        float y=384.0f+i*80.0f;
        text_shrink(c,labels[i],860,baseline(y,70,24),24,theme::kText,320);
        text_shrink(c,values[i],1778,baseline(y,70,24),24,theme::kLime,570,Align::right);
        l.line(844,y+76,1796,y+76,1,theme::kRule.with_alpha(0.5f));
    }
    text_block(c,tr("Use extracted firmware NCA files. Choose the folder in Settings, Game files; restart the app after changing it."),
               844,baseline(750,34,22),22,34,theme::kCopy,952,3,kShrink);
    if(!message_.empty()) notice_block(c,message_,844,872,20,26,
                message_warning_ ? theme::kWarning : theme::kLime,950,2,message_warning_);
    static constexpr Hint hints[]={{Pad::l1,TR("Page"),Pad::r1},{Pad::square,TR("Default")},
        {Pad::triangle,TR("Use this folder")},{Pad::circle,TR("Back")},{Pad::cross,TR("Open")}};
    draw_footer(c,hints,5);
}

// ---------------------------------------------------------------- language

void Launcher::enter_language()
{
    language_.visible = 7;
    language_.pitch = 96.0f;
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
        read_home();
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
        say(saved ? tr("Saved. Applies when a game starts.") : tr("Could not save. Please try again."),
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
    auto &list = c.list;
    draw_frame(c, tr("Language"), "");
    const auto &labels = services_.language_labels();
    list.push_clip({552.0f, 204.0f, 1008.0f, 690.0f});
    for (int row = language_.first_row(); row <= language_.last_row(); ++row)
    {
        const Rect r{570.0f, 210.0f + row * language_.pitch - language_.scroll(), 972.0f, 94.0f};
        plate(c, kRowPlate, r, row == language_.selected ? 1.0f : 0.0f);
        text_shrink(c, labels[static_cast<std::size_t>(row)], 606.0f,
                    baseline(r.y, r.h, 30.0f), 30.0f, theme::kText, 840.0f);
        if (row == prefs_.language)
        {
            list.line(1460.0f, r.y + 44.0f, 1472.0f, r.y + 56.0f, 4.0f, theme::kLime);
            list.line(1472.0f, r.y + 56.0f, 1496.0f, r.y + 30.0f, 4.0f, theme::kLime);
        }
    }
    list.pop_clip();
    scrollbar(c, language_, 1560.0f, 210.0f, 672.0f);
    if (!message_.empty())
        notice(c, message_, 570.0f, 930.0f, 24.0f,
               message_warning_ ? theme::kWarning : theme::kCopy, 972.0f, message_warning_);
    static constexpr Hint hints[] = {{Pad::circle, TR("Back")}, {Pad::cross, TR("OK")}};
    draw_footer(c, hints, 2);
}

// ---------------------------------------------------------------- about

void Launcher::draw_about(Canvas &c)
{
auto &l=c.list;
    draw_frame(c,tr("About Switch"),"");
    l.line(740,196,740,916,1,theme::kRule.with_alpha(0.6f));
    cover(c,{}, {244,270,328,328},0,0);
    text(c,"Switch",408,666,36,theme::kTitle,Align::center);
    text(c,tr("PS5 edition"),408,714,24,theme::kMeta,Align::center);
    text(c,version_,408,856,22,theme::kMeta,Align::center);
    text_shrink(c,tr("Powered by Eden"),804,baseline(238,62,34),34,theme::kTitle,974);
    text_block(c,tr("All credit for the Eden emulator goes to its developers and contributors."),
                   804,baseline(324,34,24),24,34,theme::kCopy,974,2,kShrink);
    text(c,"PS5 port: BlackBearReloaded",804,430,24,theme::kMeta);
    text(c,"Menu sounds: supplied Nintendo Switch sound bank",804,478,22,theme::kMeta);
    l.line(804,520,1796,520,1,theme::kRule);
    const std::string folder=services_.files_folder();
    const std::string labels[]={tr("KEYS"),tr("FIRMWARE"),tr("GAMES"),tr("UPDATES, DLC"),tr("MODS")};
    const std::string values[]={folder+"/keys/prod.keys",folder+"/firmware",folder+"/roms",folder+"/updates",folder+"/mods"};
    for(int i=0;i<5;++i) {
        float y=558.0f+i*63.0f;
        text_shrink(c,labels[i],804,baseline(y,54,22),22,theme::kCopy,240);
        text_shrink(c,values[i],1780,baseline(y,54,22),22,theme::kText,720,Align::right);
    }
    static constexpr Hint hints[]={{Pad::circle,TR("Back")}};
    draw_footer(c,hints,1);
}

} // namespace pe::ui
