// ProsperoEden - Launcher home: continue playing, recent games, navigation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"
#include "pe/ui/stock_motion.hpp"
#include "pe/ui/stock_assets.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace pe::ui
{

using audio::Cue;

int Launcher::home_recent_count() const
{
    return home_.status.empty() && !home_tiles_.empty() ?
        static_cast<int>(home_tiles_.size()) - 1 : 0;
}

void Launcher::rebuild_home_tiles()
{
    const int selected = home_focus_ == 0 ? 0 :
        home_focus_ >= kHomeFirstRecent && home_focus_ < kHomeAllSoftware ? home_focus_ - 6 : -1;
    const std::string selected_file = selected >= 0 && selected < static_cast<int>(home_tiles_.size()) ?
        home_tiles_[static_cast<std::size_t>(selected)].file : std::string{};
    std::vector<Recent> tiles;
    const auto add = [&](const Recent &recent) {
        if (tiles.size() >= kHomeGames || recent.file.empty() || !services_.game_exists(recent.file))
            return;
        if (std::any_of(tiles.begin(), tiles.end(), [&](const Recent &tile) { return tile.file == recent.file; }))
            return;
        auto tile = recent;
        const auto game = std::find_if(games_.begin(), games_.end(),
            [&](const Game &candidate) { return candidate.file == tile.file; });
        if (game != games_.end()) {
            tile.title = game->name;
            tile.cover = game->cover;
        }
        tiles.push_back(std::move(tile));
    };
    if (home_.last_exists)
        add({home_.last_file, home_.last_title, home_.last_cover});
    for (const auto &file : presentation_.recent_games)
        add({file, file, {}});
    for (const Recent &recent : home_.recents)
        add(recent);
    for (const Game &game : games_)
        add({game.file, game.name, game.cover});
    home_tiles_ = std::move(tiles);
    if (selected >= 0) {
        if (home_tiles_.empty()) {
            home_focus_ = home_.setup_ready ? kHomeAllSoftware : 5;
            return;
        }
        const int visible = home_.status.empty() ? static_cast<int>(home_tiles_.size()) :
                                                  std::min(1, static_cast<int>(home_tiles_.size()));
        int slot = std::min(selected, std::max(0, visible - 1));
        for (int i = 0; i < visible; ++i)
            if (home_tiles_[static_cast<std::size_t>(i)].file == selected_file)
                slot = i;
        home_focus_ = slot == 0 ? 0 : slot + 6;
    }
}
void Launcher::open_library_at_last()
{
    open(Screen::library, true);
    enter_library();
    for (int i = 0; i < static_cast<int>(games_.size()); ++i)
    {
        if (games_[static_cast<std::size_t>(i)].file == home_.last_file)
        {
            library_.reset(library_.count, i);
            refresh_selected_game();
            break;
        }
    }
}

void Launcher::press_home(Key key)
{
    const bool ready = home_.setup_ready;
    const bool continue_ready = ready && !home_tiles_.empty();
    const int recent_count = home_recent_count();
    const int before = home_focus_;
    const bool before_profile = home_profile_focus_;
    int &focus = home_focus_;

    const auto selected_options = [&]() {
        const std::string file = focus == 0 && continue_ready ? home_tiles_.front().file :
            focus >= kHomeFirstRecent && focus < kHomeFirstRecent + recent_count ? home_tiles_[static_cast<std::size_t>(focus - 6)].file : std::string{};
        if (file.empty() || !ready) { cue(Cue::error); return; }
        // Prepare the selected game's existing editor without leaving HOME.
        // Closing its modal then returns to the same tile or shortcut.
        enter_library();
        for (int i = 0; i < static_cast<int>(games_.size()); ++i) {
            if (games_[static_cast<std::size_t>(i)].file != file) continue;
            library_.reset(library_.count, i);
            refresh_selected_game();
            mode_.snap(selected_docked_ ? 0.0f : 1.0f);
            press_library(Key::triangle);
            return;
        }
        say(tr("ROM missing from the game files folder"), true);
        cue(Cue::error);
    };
    const auto tile_focus = [&](int position) {
        if (position == 0) return 0;
        return position <= recent_count ? position + 6 : kHomeAllSoftware;
    };
    if (home_profile_focus_)
    {
        const int previous_profile = home_profile_;
        if (key == Key::left || key == Key::right) {
            home_profile_ = std::clamp(home_profile_ + (key == Key::right ? 1 : -1), 0, std::max(0, static_cast<int>(profiles_.size()) - 1));
            cue(Cue::focus_account);
        } else if (key == Key::down || key == Key::circle) { home_profile_focus_ = false; cue(Cue::focus_navigation); }
        else if (key == Key::cross)
        {
            read_profiles();
            if (home_profile_ < 0 || home_profile_ >= static_cast<int>(profiles_.size()))
            {
                cue(Cue::error);
                return;
            }
            if (profiles_[static_cast<std::size_t>(home_profile_)].playing)
            {
                open_profiles();
                profile_rows_.reset(profile_row_count(), home_profile_);
            }
            else
            {
                if (services_.room_status().phase != Eden::Multiplayer::Phase::Idle) {
                    services_.leave_room();
                    say(tr("Leaving the room. Select the profile again when disconnected."));
                    return;
                }
                if (!services_.choose_profile(home_profile_))
                {
                    say(tr("Could not save. Please try again."), true);
                    cue(Cue::error);
                    return;
                }
                read_profiles();
                prefs_ = services_.preferences();
                apply_look();
                read_home();
                // The current-user marker is the confirmation; keep HOME ready.
                message_.clear();
                cue(Cue::account_select);
            }
        }
        if (previous_profile != home_profile_ || !home_profile_focus_) {
            previous_home_focus_ = -1;
            previous_home_profile_ = previous_profile;
            focus_elapsed_ = 0.0f;
        }
        return;
    }
    switch (key)
    {
    case Key::up:
        if (focus >= 1 && focus <= 6) focus = 0;
        else if (!profiles_.empty()) home_profile_focus_ = true;
        break;
    case Key::down:
        if (focus == 0 || focus >= kHomeFirstRecent) focus = 1;
        break;
    case Key::left:
    case Key::right:
    {
        const int delta = key == Key::right ? 1 : -1;
        if (focus >= 1 && focus <= 6) {
            focus = std::clamp(focus + delta, 1, 6);
        } else {
            const int length = recent_count + 2;
            int position = focus == 0 ? 0 : focus == kHomeAllSoftware ? length - 1 : focus - 6;
            focus = tile_focus(std::clamp(position + delta, 0, length - 1));
        }
        break;
    }
    case Key::triangle:
        selected_options();
        return;
    case Key::cross:
        press_ = 1.0f;
        if (focus == 0 && continue_ready)
        {
            const Recent &game = home_tiles_.front();
            request_launch(game.file, game.title, game.cover);
        }
        else if ((focus == 0 || focus == kHomeAllSoftware) && ready)
        {
            open(Screen::library, true);
            enter_library();
        }
        else if (focus == 5)
        {
            open(Screen::settings, true);
            prefs_ = services_.preferences();
            section_.snap(1.0f);
            settings_content_ = false;
        }
        else if (focus == 1)
        {
            open(Screen::about, true);
        }
        else if (focus == 3)
        {
            open_multiplayer();
            cue(Cue::album_open);
        }
        else if (focus == 2) { open(Screen::files, true); enter_files(); }
        else if (focus == 4)
        {
            prefs_ = services_.preferences();
            open_mapping(false);
        }
        else if (focus == 6) { screen_off_ = true; cue(Cue::select); }
        else if (focus >= kHomeFirstRecent && focus < kHomeFirstRecent + recent_count && ready)
        {
            const Recent &recent = home_tiles_[static_cast<std::size_t>(focus - 6)];
            request_launch(recent.file, recent.title, recent.cover);
        }
        else
        {
            cue(Cue::error);
        }
        return;
    default:
        return;
    }
    if (focus != before || home_profile_focus_ != before_profile)
    {
        previous_home_focus_ = before;
        previous_home_profile_ = -1;
        focus_elapsed_ = 0.0f;
        cue(Cue::focus);
        if ((focus == 0 || focus >= kHomeFirstRecent) && (key == Key::left || key == Key::right)) cue(Cue::home_scroll);
    }
    else if (key == Key::left || key == Key::right) cue(Cue::scroll_limit);
}

void Launcher::update_controllers(float dt)
{
    const unsigned now = services_.controllers() & 0xfu;
    bool joined = false;
    bool left = false;
    for (int player = 0; player < 4; ++player)
    {
        const std::size_t index = static_cast<std::size_t>(player);
        const bool on = (now >> player & 1u) != 0;
        controller_lit_[index].target = on ? 1.0f : 0.0f;
        if (!controllers_known_)
        {
            controller_lit_[index].snap(controller_lit_[index].target);
        }
        else if (on != ((controllers_ >> player & 1u) != 0))
        {
            (on ? joined : left) = true;
            if (on)
                controller_pop_[index] = 1.0f;
        }
        controller_lit_[index].update(dt, 10.0f);
        controller_pop_[index] = std::max(0.0f, controller_pop_[index] - dt / 0.5f);
    }
    // One sound however many changed at once; none while a game is starting.
    if ((joined || left) && selected_game_.empty())
        cue(joined ? Cue::saved : Cue::modal_close);
    controllers_ = now;
    controllers_known_ = true;
}

void Launcher::draw_controllers(Canvas &c)
{
for (int player=0; player<4; ++player) {
        const float lit=tween::clamp01(controller_lit_[static_cast<std::size_t>(player)].value);
        controller_icon(c,{108.0f+66.0f*player,1007.0f,50.4f,35.0f},lit);
    }
}

void Launcher::draw_home(Canvas &c)
{
    auto &l = c.list;
    const int count = home_recent_count();
    float frame = 60.0f * intro_;
    float games_alpha = (first_start_ ? stock_motion::home_starter_alpha(frame) : stock_motion::home_entry_alpha(frame)) / 255.0f;
    float games_scale = first_start_ ? stock_motion::home_starter_scale(frame) : stock_motion::home_entry_scale(frame);
    float applet_alpha = games_alpha;
    float applet_scale = games_scale;
    if (transition_.running) {
        frame = transition_.elapsed * 60.0f;
        if (screen_ != Screen::home) {
            games_alpha = stock_motion::home_game_in_alpha(frame) / 255.0f;
            games_scale = stock_motion::home_game_in_scale(frame);
            applet_alpha = stock_motion::home_applet_in_alpha(frame) / 255.0f;
            applet_scale = stock_motion::home_applet_in_scale(frame);
        } else {
            frame = std::max(0.0f, frame - 6.0f);
            games_alpha = stock_motion::home_game_out_alpha(frame) / 255.0f;
            games_scale = stock_motion::home_game_out_scale(frame);
            applet_alpha = stock_motion::home_applet_out_alpha(frame) / 255.0f;
            applet_scale = stock_motion::home_applet_out_scale(std::min(frame,20.0f));
        }
    }
    l.push_opacity(games_alpha);
    for (int i = 0; i < static_cast<int>(profiles_.size()); ++i) {
        const Rect r{100.5f + 90.0f * i, 62.0f, 72.0f, 72.0f};
        const auto avatar = presentation_.avatar(profiles_[static_cast<std::size_t>(i)].name);
        profile_avatar(c, avatar.icon, avatar.color, r, true);
        l.ring(r.x + 36.0f, r.y + 36.0f, 37.0f, 2.0f, theme::kTitle.with_alpha(0.7f));
        if (profiles_[static_cast<std::size_t>(i)].playing &&
            !(home_profile_focus_ && i == home_profile_))
            l.rounded_rect({r.x + 16.0f, r.y + r.h + 10.0f, 40.0f, 4.0f}, 2.0f, theme::kLime);
        if (i == previous_home_profile_ && focus_elapsed_ < .05f)
            stock_focus(c,r,stock_motion::home_unfocus_alpha(focus_elapsed_*60)/255.0f,focus_elapsed_,true);
        if (home_profile_focus_ && i == home_profile_) {
            stock_focus(c, r, 1.0f, focus_elapsed_, true);
            text_fit(c, profiles_[static_cast<std::size_t>(i)].name, r.x + 36.0f, 178.0f, 28.0f,
                     theme::kLime, 240.0f, Align::center);
        }
    }
    text(c, clock_, 1814.0f, baseline(80,50,30), 30.0f, theme::kText, Align::right);
    // Stock MiniSet/HudSignal wireless artwork beside the clock reports the
    // multiplayer room connection, not the console's Internet/Wi-Fi state.
    const auto room_phase = room_status_.phase;
    const bool connected = room_phase == Eden::Multiplayer::Phase::Connected;
    const bool changing = room_phase == Eden::Multiplayer::Phase::Connecting ||
                          room_phase == Eden::Multiplayer::Phase::Disconnecting;
    const auto signal = connected ? stock_assets::Icon::wifi_connected :
                        changing ? stock_assets::Icon::wifi_connecting :
                                   stock_assets::Icon::wifi_unset;
    if (c.textures.stock_ui())
        l.image(c.textures.stock_ui(), {1832.0f, 81.0f, 48.0f, 48.0f},
                stock_assets::icon_uv(signal), theme::kText);
    l.pop_opacity();

    const int selected_slot = home_focus_ >= kHomeFirstRecent && home_focus_ < kHomeFirstRecent + count ? home_focus_ - 6 :
                              home_focus_ == kHomeAllSoftware ? count + 1 : 0;
    std::string selected = selected_slot <= count && !home_tiles_.empty() ? home_tiles_[static_cast<std::size_t>(selected_slot)].title : tr("All Software");
    const float center = kHomeTileX + kHomeTileSize * 0.5f + selected_slot * kHomeTilePitch - home_strip_.value;
    if (!home_profile_focus_ && (home_focus_ == 0 || home_focus_ >= kHomeFirstRecent)) {
        const float width = std::min(900.0f, std::max(450.0f, text_width(c, selected, 32.0f)));
        const float x = std::clamp(center - width * 0.5f, 112.0f, 1808.0f - width);
        l.push_opacity(games_alpha);
        text_marquee(c, selected, {x, 222.0f, width, 52.0f}, 32.0f, theme::kLime, focus_elapsed_, Align::center);
        l.pop_opacity();
    }

    l.push_opacity(games_alpha);
    l.push_transform(1.0f + (games_scale - 1.0f) * motion(), 960, 483, 0, 0);
    l.push_clip({0, 278, 1920, 426});
    const auto tile = [&](int slot, int id, const std::string &path, bool all) {
        Rect r{kHomeTileX + kHomeTilePitch * slot - home_strip_.value, kHomeTileY, kHomeTileSize, kHomeTileSize};
        const bool focus = id == home_focus_ && !home_profile_focus_;
        const bool starting = !selected_game_.empty() && focus && !all;
        const float launch_frame = launch_.elapsed * 60.0f;
        const float scale = starting ? stock_motion::decide_scale(launch_frame) : 1.0f;
        l.push_transform(1.0f + (scale - 1.0f) * motion(), r.x+r.w*.5f, r.y+r.h*.5f, 0, 0);
        if (all) {
            r = {r.x + 72.0f, r.y + 72.0f, 240.0f, 240.0f};
            l.shadow({r.x, r.y + 1.5f, r.w, r.h},120,7.5f,Color::rgb(0,0.4f));
            l.circle(r.x+120,r.y+120,120,theme::kShortcut);
            l.push_transform(1.8f, r.x+120, r.y+120, 0, 0);
            home_symbol(c,0,r.x+120,r.y+120,theme::kTitle);
            l.pop_transform();
        } else cover(c, path, r, 0.0f, 1.0f);
        if (!focus && id == previous_home_focus_ && focus_elapsed_ < .05f)
            stock_focus(c,r,stock_motion::home_unfocus_alpha(focus_elapsed_*60)/255.0f,focus_elapsed_,all);
        if (focus) stock_focus(c, r, stock_motion::home_focus_alpha(focus_elapsed_*60.0f)/255.0f, focus_elapsed_, all);
        if (starting) {
            l.rounded_rect(r,0,Color::rgb(0xffffff,stock_motion::decide_flash(launch_frame)/255.0f));
            const float effect_scale = stock_motion::launch_effect_scale(launch_frame);
            const float effect_size = 450.0f * effect_scale;
            // Keep the mirrored launch light inside the pressed artwork. Its
            // expanding quad must not leave a second rectangle around the tile
            // or wash out the focus border and adjacent covers.
            if(c.textures.stock_ui()) {
                l.push_clip(r);
                l.image(c.textures.stock_ui(),
                    {r.x+r.w*.5f-effect_size*.5f,r.y+r.h*.5f-effect_size*.5f,effect_size,effect_size},
                    stock_assets::icon_uv(stock_assets::Icon::launch_effect),
                    Color::rgb(0xffffff,stock_motion::launch_effect_alpha(launch_frame)/255.0f));
                l.pop_clip();
            }
        }
        l.pop_transform();
    };
    tile(0,0,!home_tiles_.empty() ? home_tiles_.front().cover : std::string{},false);
    for (int i=0;i<count;++i) tile(i+1,i+kHomeFirstRecent,home_tiles_[static_cast<std::size_t>(i+1)].cover,false);
    tile(count+1,kHomeAllSoftware,{},true);
    l.pop_clip();
    l.pop_transform();
    l.pop_opacity();

    if (!message_.empty()) notice_block(c,message_,160.5f,baseline(706,32,22),22,32,
        message_warning_ ? theme::kWarning : theme::kLime,1680,2,message_warning_);
    else if (!home_.status.empty()) notice_block(c,home_.status,160.5f,baseline(706,32,22),22,32,theme::kCopy,1680,2,home_.launch_failed);
    else if (home_tiles_.empty()) text_shrink(c,tr("Games you launch will appear here."),160.5f,731,22,theme::kMeta,1680);

    const char *names[] = {TR("About"),TR("Game files"),TR("Multiplayer"),TR("Controllers"),TR("System Settings"),TR("Screen off")};
    const Color inks[] = {Color::rgb(0xff4d4d),Color::rgb(0xffae2e),Color::rgb(0x4a9bdb),theme::kTitle,theme::kTitle,theme::kTitle};
    l.push_opacity(applet_alpha);
    l.push_transform(1+(applet_scale-1)*motion(),960,816,0,0);
    for (int i=0;i<6;++i) {
        const float x=555.0f+162*i,y=816.0f;
        l.shadow({x-66,y-64.5f,132,132},66,7.5f,Color::rgb(0,.35f));
        l.circle(x,y,66,theme::kShortcut);
        if (previous_home_focus_ == i+1 && focus_elapsed_ < .05f)
            stock_focus(c,{x-66,y-66,132,132},stock_motion::home_unfocus_alpha(focus_elapsed_*60)/255.0f,focus_elapsed_,true);
        if (!home_profile_focus_ && home_focus_==i+1) {
            stock_focus(c,{x-66,y-66,132,132},1,focus_elapsed_,true);
            text_shrink(c,tr(names[i]),x,baseline(892,40,24),24,theme::kLime,330,Align::center);
        }
        applet_symbol(c,i,x,y,inks[i]);
    }
    l.pop_transform();
    l.pop_opacity();
    if (draw_chrome_) {
        static constexpr Hint hints[]={{Pad::triangle,TR("Game settings")},{Pad::cross,TR("Select")}};
        draw_footer(c,hints,2);
    }
}

} // namespace pe::ui
