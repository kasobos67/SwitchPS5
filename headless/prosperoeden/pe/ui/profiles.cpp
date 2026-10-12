// ProsperoEden - Launcher: the profiles (Settings > Profiles): who is playing, a new one, another
// name, one taken off the list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"
#include "pe/ui/stock_assets.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace pe::ui
{

using audio::Cue;

namespace
{

// Five profile rows are visible; the list uses the existing scroll pitch.
constexpr float kRowPitch = 96.0f;
constexpr int kRowsShown = 5;
constexpr int kMostProfiles = 8;

} // namespace

void profile_avatar(Canvas &c, int icon, int color, const Rect &r, bool circular)
{
    const auto fill=stock_assets::profile_color(color);
    c.list.rounded_rect(r,circular ? r.w*.5f : 0.0f,fill);
    if(c.textures.stock_profiles()) c.list.rounded_image(c.textures.stock_profiles(),r,
        stock_assets::profile_uv(icon),circular ? r.w*.5f : 0.0f,Color::rgb(0xffffff));
}

void Launcher::read_profiles()
{
    profiles_ = services_.profiles();
    playing_.clear();
    for (const Profile &profile : profiles_)
        if (profile.playing)
            playing_ = profile.name;
}

int Launcher::profile_row_count() const
{
    // The profiles, then "New profile" while there is room for one.
    const int count = static_cast<int>(profiles_.size());
    return count < kMostProfiles ? count + 1 : count;
}

void Launcher::open_profiles(bool for_launch)
{
    read_profiles();
    int playing = 0;
    for (std::size_t i = 0; i < profiles_.size(); ++i)
        if (profiles_[i].playing)
            playing = static_cast<int>(i);
    profile_rows_.visible = kRowsShown;
    profile_rows_.pitch = kRowPitch;
    profile_rows_.reset(for_launch ? static_cast<int>(profiles_.size()) : profile_row_count(), playing);
    profile_remove_ = -1;
    profile_for_launch_ = for_launch;
    profile_editing_ = false;
    open_modal(Modal::profiles);
    message_.clear();
}

void Launcher::press_profiles(Key key)
{
    const int count = static_cast<int>(profiles_.size());
    const int row = std::clamp(profile_rows_.selected, 0, std::max(profile_row_count() - 1, 0));
    const bool on_profile = row < count;
    if (profile_editing_)
    {
        int &choice = profile_edit_color_ ? profile_color_choice_ : profile_avatar_choice_;
        const int total = profile_edit_color_ ? stock_assets::kProfileColorCount : stock_assets::kProfileIconCount;
        if (key == Key::circle)
        {
            if (profile_edit_color_)
                profile_edit_color_ = false;
            else
                profile_editing_ = false;
            cue(Cue::back);
        }
        else if (key == Key::left || key == Key::right || key == Key::up || key == Key::down)
        {
            const int delta = key == Key::left ? -1 : key == Key::right ? 1 : key == Key::up ? -8 : 8;
            choice = std::clamp(choice + delta, 0, total - 1);
            cue(Cue::focus);
        }
        else if (key == Key::cross)
        {
            if (!profile_edit_color_)
            {
                profile_edit_color_ = true;
                cue(Cue::select);
            }
            else if (on_profile && set_profile_avatar(profiles_[static_cast<std::size_t>(row)].name,
                                                     profile_avatar_choice_, profile_color_choice_))
            {
                profile_editing_ = false;
                cue(Cue::saved);
            }
            else
            {
                say(tr("Could not save. Please try again."), true);
                cue(Cue::error);
            }
        }
        return;
    }
    // Taking a profile off the list is asked twice; anything else forgets the first press.
    const int armed = profile_remove_;
    profile_remove_ = -1;
    switch (key)
    {
    case Key::circle:
        close_modal();
        if (profile_for_launch_)
            pending_launch_ = {};
        profile_for_launch_ = false;
        message_.clear();
        cue(Cue::back);
        return;
    case Key::up:
    case Key::down:
    case Key::left:
    case Key::right:
        if (profile_rows_.move(key == Key::down || key == Key::right ? 1 : -1))
        {
            message_.clear();
            cue(Cue::focus);
        }
        return;
    case Key::cross:
        if (!on_profile)
        {
            const int added = services_.add_profile();
            if (added < 0)
            {
                say(tr("Could not save. Please try again."), true);
                cue(Cue::error);
                return;
            }
            read_profiles();
            profile_rows_.reset(profile_row_count(), added);
            say(tr("Added. Cross selects this user; Triangle changes their name."));
            cue(Cue::toggle);
            return;
        }
        if (profiles_[static_cast<std::size_t>(row)].playing)
        {
            if (profile_for_launch_)
            {
                cue(Cue::account_select);
                launch_pending();
                if(!selected_game_.empty()) close_modal();
                return;
            }
            say(fill(tr("Playing as {0}"), {profiles_[static_cast<std::size_t>(row)].name}));
            cue(Cue::focus);
            return;
        }
        if (services_.room_status().phase != Eden::Multiplayer::Phase::Idle) {
            services_.leave_room();
            say(tr("Leaving the room. Select the profile again when disconnected."));
            return;
        }
        if (!services_.choose_profile(row))
        {
            say(tr("Could not save. Please try again."), true);
            cue(Cue::error);
            return;
        }
        read_profiles();
        // Its settings, its game on the home screen and its recently played games are its own.
        prefs_ = services_.preferences();
        apply_look();
        read_home();
        if (profile_for_launch_)
        {
            cue(Cue::account_select);
            launch_pending();
            if(!selected_game_.empty()) close_modal();
            return;
        }
        say(fill(tr("Playing as {0}"), {playing_}));
        cue(Cue::toggle);
        return;
    case Key::triangle:
    {
        if (!on_profile)
            return;
        const auto old_name=profiles_[static_cast<std::size_t>(row)].name;
        const auto old_avatar=presentation_.avatar(old_name);
        if (!services_.rename_profile(row, 1))
        {
            say(tr("Could not save. Please try again."), true);
            cue(Cue::error);
            return;
        }
        read_profiles();
        if (row < static_cast<int>(profiles_.size())) set_profile_avatar(profiles_[static_cast<std::size_t>(row)].name,old_avatar.icon,old_avatar.color);
        message_.clear();
        cue(Cue::account_select);
        return;
    }
    case Key::options:
        if (on_profile && !profile_for_launch_)
        {
            const auto avatar = presentation_.avatar(profiles_[static_cast<std::size_t>(row)].name);
            profile_avatar_choice_ = avatar.icon;
            profile_color_choice_ = avatar.color;
            profile_edit_color_ = false;
            profile_editing_ = true;
            message_.clear();
            cue(Cue::open);
        }
        return;
    case Key::square:
        if (!on_profile)
            return;
        if (profiles_[static_cast<std::size_t>(row)].playing || count < 2)
        {
            say(tr("The profile that is playing stays. Choose another one first."), true);
            cue(Cue::error);
            return;
        }
        if (armed != row)
        {
            profile_remove_ = row;
            say(fill(tr("Square again takes {0} off the list. Its save data stays on the console."),
                     {profiles_[static_cast<std::size_t>(row)].name}),
                true);
            cue(Cue::notify);
            return;
        }
        if (!services_.remove_profile(row))
        {
            say(tr("Could not save. Please try again."), true);
            cue(Cue::error);
            return;
        }
        read_profiles();
        profile_rows_.reset(profile_row_count(), std::min(row, static_cast<int>(profiles_.size()) - 1));
        say(tr("Taken off the list. Its save data stays on the console."));
        cue(Cue::toggle);
        return;
    default:
        return;
    }
}

void Launcher::draw_profiles(Canvas &c, float open)
{
    auto &list = c.list;
    list.push_opacity(open);
    const int count = static_cast<int>(profiles_.size());
    const int selected = std::clamp(profile_rows_.selected, 0, std::max(profile_row_count() - 1, 0));
    const Profile *profile = selected < count ? &profiles_[static_cast<std::size_t>(selected)] : nullptr;
    if (profile_editing_ && profile)
    {
        // Icon editing is a page of its own. An inset panel lets the previous
        // screen show around its edges, especially when opened from the library.
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, theme::kPanel);
        text(c, tr(profile_edit_color_ ? "Choose a Background" : "Choose an Icon"), 96.0f, 112.0f,
             40.0f, theme::kTitle);
        list.line(96.0f, 157.0f, 1824.0f, 157.0f, 1.0f, theme::kRule);
        list.line(520.0f, 195.0f, 520.0f, 914.0f, 1.0f, theme::kRule);
        const int total = profile_edit_color_ ? stock_assets::kProfileColorCount : stock_assets::kProfileIconCount;
        const int choice = profile_edit_color_ ? profile_color_choice_ : profile_avatar_choice_;
        constexpr float size = 114.0f;
        constexpr float pitch = 144.0f;
        list.push_clip({560,195,1168,714});
        const float scroll=std::max(0,choice/8-4)*120.0f;
        for (int cell = 0; cell < total; ++cell)
        {
            const Rect r{570.0f + (cell % 8) * pitch, 206.0f + (cell / 8) * 120.0f-scroll, size, size};
            profile_avatar(c, profile_edit_color_ ? profile_avatar_choice_ : cell,
                                profile_edit_color_ ? cell : profile_color_choice_, r, false);
            if (cell == choice)
                stock_focus(c,r,1.0f,focus_elapsed_);
        }
        list.pop_clip();
        profile_avatar(c, profile_avatar_choice_, profile_color_choice_, {174.0f, 309.0f, 300.0f, 300.0f}, false);
        text_shrink(c, profile->name, 324.0f, 669.0f, 30.0f, theme::kText, 348.0f, Align::center);
        if (!message_.empty())
            notice(c, message_, 570.0f, 922.0f, 24.0f, theme::kWarning, 1152.0f, true);
    }
    else if (profile_for_launch_)
    {
        // The launch chooser uses the original square 96px account cards at 720p.
        glass(c, {150.0f, 288.0f, 1620.0f, 522.0f}, 0.0f, theme::kPanel, theme::kPanelEdge, 1.0f);
        text(c, tr("Select a User"), 960.0f, 367.0f, 36.0f, theme::kTitle, Align::center);
        const float left = 960.0f - (count * 180.0f - 36.0f) * 0.5f;
        for (int user = 0; user < count; ++user)
        {
            const auto &item = profiles_[static_cast<std::size_t>(user)];
            const auto avatar = presentation_.avatar(item.name);
            const Rect r{left + user * 180.0f, 447.0f, 144.0f, 144.0f};
            profile_avatar(c, avatar.icon, avatar.color, r, false);
            if (user == selected)
                stock_focus(c,r,1.0f,focus_elapsed_);
            text_shrink(c, item.name, r.x + 72.0f, 645.0f, 26.0f,
                        user == selected ? theme::kLime : theme::kCopy, 168.0f, Align::center);
        }
        if (!message_.empty())
            notice(c, message_, 210.0f, 730.0f, 24.0f, theme::kWarning, 1500.0f, true);
    }
    else
    {
        list.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0.0f, theme::kPanel);
        const auto avatar = profile ? presentation_.avatar(profile->name) : presentation_.avatar("");
        profile_avatar(c, avatar.icon, avatar.color, {141.0f, 62.0f, 76.0f, 76.0f}, true);
        const std::string title = profile ? fill(tr("{0}'s Page"), {profile->name}) : tr("Add User");
        text_shrink(c, title, 238.0f, 116.0f, 40.0f, theme::kTitle, 1500.0f);
        list.line(96.0f, 157.0f, 1824.0f, 157.0f, 1.0f, theme::kRule);
        for (int user = 0; user < profile_row_count(); ++user)
        {
            const Rect r{156.0f, 228.0f + user * 78.0f, 438.0f, 76.0f};
            if (user == selected)
            {
                list.rounded_rect({171.0f, r.y + 13.0f, 4.0f, 50.0f}, 0.0f, theme::kLime);
                plate_focus(c, kRowPlate, r, 1.0f);
            }
            text_shrink(c, user < count ? profiles_[static_cast<std::size_t>(user)].name : tr("Add User"),
                        198.0f, baseline(r.y, r.h, 28.0f), 28.0f,
                        user == selected ? theme::kLime : theme::kText, 372.0f);
        }
        if (profile)
        {
            profile_avatar(c, avatar.icon, avatar.color, {714.0f, 220.0f, 312.0f, 312.0f}, false);
            const Rect name{1062.0f, 220.0f, 738.0f, 112.0f};
            plate(c, kRowPlate, name, 1.0f);
            text_shrink(c, profile->name, 1098.0f, baseline(name.y, name.h, 40.0f), 40.0f,
                        theme::kText, 652.0f);
            text(c, tr(profile->playing ? "Current User" : "Select this User"), 1098.0f, 403.0f,
                 30.0f, profile->playing ? theme::kLime : theme::kCopy);
            list.line(1062.0f, 452.0f, 1800.0f, 452.0f, 1.0f, theme::kRule);
            text_block(c, tr("Save data and settings are kept separately for each user."),
                       1098.0f, 495.0f, 24.0f, 36.0f, theme::kCopy, 666.0f, 2, kShrink);
            text(c, tr("Software"), 714.0f, 602.0f, 30.0f, theme::kText);
            list.line(714.0f, 627.0f, 1800.0f, 627.0f, 1.0f, theme::kRule);
            if (profile->playing && !home_tiles_.empty())
            {
                const int shown = std::min(3, static_cast<int>(home_tiles_.size()));
                for (int row = 0; row < shown; ++row)
                {
                    const auto &recent = home_tiles_[static_cast<std::size_t>(row)];
                    const float y = 645.0f + row * 94.0f;
                    cover(c, recent.cover, {714.0f, y, 78.0f, 78.0f}, 0.0f, 0.3f);
                    text_fit(c, recent.title, 816.0f, baseline(y, 78.0f, 27.0f), 27.0f,
                             theme::kText, 984.0f);
                    list.line(714.0f, y + 88.0f, 1800.0f, y + 88.0f, 1.0f, theme::kRule);
                }
            }
            else
                text_block(c, tr("Select this user to view their software."), 714.0f, 687.0f,
                           26.0f, 39.0f, theme::kCopy, 1086.0f, 2, kShrink);
        }
        else
        {
            text(c, tr("Add a user to this console."), 714.0f, 350.0f, 30.0f, theme::kText);
            text_block(c, tr("Each user keeps their own save data, settings and recent games."),
                       714.0f, 435.0f, 26.0f, 39.0f, theme::kCopy, 1086.0f, 3, kShrink);
        }
        if (!message_.empty())
            notice(c, message_, 714.0f, 929.0f, 24.0f,
                   message_warning_ ? theme::kWarning : theme::kCopy, 1086.0f, message_warning_);
    }
    static constexpr Hint choices[] = {{Pad::circle, TR("Back")}, {Pad::cross, TR("OK")}};
    static constexpr Hint users[] = {{Pad::options, TR("Edit Icon")}, {Pad::triangle, TR("Rename")}, {Pad::square, TR("Remove")},
        {Pad::circle, TR("Back")}, {Pad::cross, TR("Select")}};
    draw_footer(c, !profile_for_launch_ && !profile_editing_ && profile ? users : choices,
                !profile_for_launch_ && !profile_editing_ && profile ? 5 : 2);
    list.pop_opacity();
}

} // namespace pe::ui
