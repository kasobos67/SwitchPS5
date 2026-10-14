// ProsperoEden - Launcher library: the game list, its details and per-game settings.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"
#include "pe/ui/stock_motion.hpp"
#include "pe/ui/stock_assets.hpp"

#include "pe/core/log.hpp"

#include <cctype>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>

namespace pe::ui
{

using audio::Cue;

namespace
{

// The game settings dialog's rows, and the window that shows five of them (also the Mods list's).
// Video to Language open what the game does differently from Settings (game_options.cpp).
enum GameRow : int
{
    row_mode,
    row_video,
    row_performance,
    row_audio,
    row_controls,
    row_system,
    row_language,
    row_mods,
    row_save,   // in builds that move saves
    row_delete, // a game a download source has: deleted from the console
};
constexpr float kDialogRowsTop = 334.0f;
constexpr float kDialogRowPitch = 96.0f;
constexpr float kDialogRowHeight = 94.0f;
constexpr int kDialogRowsShown = 5;
constexpr Rect kDialogWindow{592.0f, kDialogRowsTop, 736.0f,
                             kDialogRowPitch * (kDialogRowsShown - 1) + kDialogRowHeight};
constexpr float kDialogHints = 848.0f;

} // namespace

void Launcher::start_scan()
{
    if (!scan_.valid() && home_.setup_ready)
        scan_ = std::async(std::launch::async, [this] { return services_.games(); });
}

// A game's name as it is shown: with the characters a font has, the launcher's own or the
// console's. One with nothing left that says anything (a name in a script no font here has, or
// on a console whose fonts could not be read) is replaced by the name its file gives, rather than
// drawn as question marks.
static std::string shown_name(const gfx::Font *font, const std::string &name, const std::string &file)
{
    if (font == nullptr || name.empty() || font->can_draw(name))
        return name;
    std::string kept;
    for (const char c : font->drawable(name))
        if (c != ' ' || (!kept.empty() && kept.back() != ' '))
            kept += c;
    while (!kept.empty() && kept.back() == ' ')
        kept.pop_back();
    const bool says = std::any_of(kept.begin(), kept.end(), [](char c)
                                  { return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                                           static_cast<unsigned char>(c) >= 0x80; });
    if (says)
        return kept;
    const std::string stem = file.substr(0, file.find_last_of('.'));
    return stem.empty() ? name : stem;
}

void Launcher::finish_scan(bool wait)
{
    if (!scan_.valid())
        return;
    if (!wait && modal_shown_ != Modal::none)
        return; // retain the dialog target until its closing animation ends
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
    for (Game &game : games_)
        game.name = shown_name(fonts_.font, game.name, game.file);
    std::stable_sort(games_.begin(), games_.end(), [this](const Game &a, const Game &b) {
        return library_sort_ == 1 ? a.file < b.file : a.name < b.name;
    });
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
    rebuild_home_tiles();
}

void Launcher::read_home()
{
    home_ = services_.home();
    home_.last_title = shown_name(fonts_.font, home_.last_title, home_.last_file);
    for (Recent &recent : home_.recents)
        recent.title = shown_name(fonts_.font, recent.title, recent.file);
    // Keep the service's diagnostic wording and substitutions, with this edition's display name.
    for (std::size_t at = 0; (at = home_.status.find("ProsperoEden", at)) != std::string::npos;) {
        home_.status.replace(at, 12, "Switch");
        at += 6;
    }
    // HOME is a single row: do not show its leading game again in recents.
    home_.recents.erase(std::remove_if(home_.recents.begin(), home_.recents.end(),
        [this](const Recent &g) { return home_.last_exists && g.file == home_.last_file; }), home_.recents.end());
    rebuild_home_tiles();
    if (home_.last_title_id == 0)
        return;
    const std::vector<Mod> mods = services_.mods(home_.last_title_id);
    home_.last_mods = static_cast<int>(mods.size());
    // With the game's Mods switch off none of them is on.
    const bool enabled = mods.empty() || services_.mods_enabled(home_.last_title_id);
    home_.last_mods_on = !enabled ? 0 : static_cast<int>(
        std::count_if(mods.begin(), mods.end(), [](const Mod &mod) { return mod.enabled; }));
}

void Launcher::check_games_present()
{
    // The home screen: its game, or one of its recent ones, is gone. Its focus stays where it can.
    if (screen_ == Screen::home && modal_ == Modal::none && modal_shown_ == Modal::none)
    {
        bool gone = home_.last_exists && !services_.game_exists(home_.last_file);
        for (const Recent &recent : home_tiles_)
            gone = gone || !services_.game_exists(recent.file);
        if (gone)
        {
            read_home();
        }
    }
    // The Library's list, whatever screen shows: a game's own dialogs stay with their game.
    if (modal_ == Modal::none && modal_shown_ == Modal::none)
        drop_missing_games();
}

bool Launcher::drop_missing_games()
{
    if (!games_loaded_)
        return false;
    const std::string selected =
        library_.selected < static_cast<int>(games_.size()) ? games_[static_cast<std::size_t>(library_.selected)].file :
                                                              std::string{};
    // A game on download sources has no file yet: it leaves when their lists do not have it.
    const auto gone = std::remove_if(games_.begin(), games_.end(), [this](const Game &game)
                                     { return !game.remote && !services_.game_exists(game.file); });
    if (gone == games_.end())
        return false;
    games_.erase(gone, games_.end());
    // The selection stays on its game, or on the one that took its place.
    int index = std::min(library_.selected, std::max(0, static_cast<int>(games_.size()) - 1));
    for (int i = 0; i < static_cast<int>(games_.size()); ++i)
        if (games_[static_cast<std::size_t>(i)].file == selected)
            index = i;
    library_.reset(static_cast<int>(games_.size()), index);
    refresh_selected_game();
    rebuild_home_tiles();
    return true;
}

void Launcher::count_mods(Game &game, const std::vector<Mod> &mods)
{
    game.mods = static_cast<int>(mods.size());
    // The game's Mods switch (the Library's) comes first: off, none of its mods is used, whatever
    // their own switches say.
    game.mods_enabled = mods.empty() || services_.mods_enabled(game.title_id);
    game.mods_on = !game.mods_enabled ? 0 : static_cast<int>(
        std::count_if(mods.begin(), mods.end(), [](const Mod &mod) { return mod.enabled; }));
    // The home screen says the same of its game.
    if (game.file == home_.last_file && home_.last_title_id != 0)
    {
        home_.last_mods = game.mods;
        home_.last_mods_on = game.mods_on;
    }
}

void Launcher::read_mods(Game &game)
{
    mods_ = services_.mods(game.title_id);
    count_mods(game, mods_);
    mod_list_.clear();
    for (int mod = 0; mod < static_cast<int>(mods_.size()); ++mod)
    {
        mod_list_.push_back({mod, -1});
        for (int cheat = 0; cheat < static_cast<int>(mods_[static_cast<std::size_t>(mod)].cheats.size());
             ++cheat)
            mod_list_.push_back({mod, cheat});
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
        // Games copied to the console since the list was read appear in a moment; games taken
        // away leave it at once.
        finish_scan(false);
        drop_missing_games();
        start_scan();
    }
    else
    {
        start_scan();
        finish_scan(true);
    }
    library_.visible = 8;
    library_.pitch = 88.0f;
    library_.reset(static_cast<int>(games_.size()), 0);
    library_scroll_.snap(0.0f);
    refresh_selected_game();
    detail_.snap(1.0f);
    mode_.snap(selected_docked_ ? 0.0f : 1.0f);
}

void Launcher::refresh_selected_game()
{
    const std::uint64_t id =
        games_.empty() ? 0 : games_[static_cast<std::size_t>(library_.selected)].title_id;
    selected_docked_ = id == 0 || services_.docked(id);
    // Its Mods switch shows its state at once; it only animates when changed.
    mods_switch_.snap(!games_.empty() && games_[static_cast<std::size_t>(library_.selected)].mods_enabled ?
                          1.0f : 0.0f);
    detail_.value = 0.0f;
    detail_.velocity = 0.0f;
}

void Launcher::press_library(Key key)
{
    if (library_sort_open_)
    {
        if (key == Key::up || key == Key::down) { library_sort_choice_ = 1 - library_sort_choice_; cue(Cue::focus_menu); }
        else if (key == Key::circle) { library_sort_open_ = false; cue(Cue::back); }
        else if (key == Key::cross) {
            const std::string file = games_.empty() ? std::string{} : games_[static_cast<std::size_t>(library_.selected)].file;
            library_sort_ = library_sort_choice_;
            std::stable_sort(games_.begin(), games_.end(), [this](const Game &a,const Game &b) { return library_sort_ == 1 ? a.file < b.file : a.name < b.name; });
            for (int i=0;i<static_cast<int>(games_.size());++i) if (games_[static_cast<std::size_t>(i)].file == file) library_.reset(static_cast<int>(games_.size()),i);
            library_sort_open_ = false; focus_elapsed_ = 0; cue(Cue::select);
        }
        return;
    }
    const int count = static_cast<int>(games_.size());
    const Game *game = count > 0 ? &games_[static_cast<std::size_t>(library_.selected)] : nullptr;
    switch (key)
    {
    case Key::circle:
        home_focus_ = kHomeAllSoftware;
        open(Screen::home, false);
        return;
    case Key::up:
    case Key::down:
    case Key::left:
    case Key::right:
    case Key::l1:
    case Key::r1:
    {
        if (count == 0) return;
        const int before = library_.selected;
        const int delta = key == Key::up ? -kLibraryColumns : key == Key::down ? kLibraryColumns :
                          key == Key::left ? -1 : key == Key::right ? 1 : key == Key::l1 ? -18 : 18;
        const int next = std::clamp(before + delta, 0, count - 1);
        if (next != before) {
            previous_library_focus_ = before;
            library_.reset(count, next);
            message_.clear(); refresh_selected_game();
            mode_.snap(selected_docked_ ? 0.0f : 1.0f);
            focus_elapsed_ = 0;
            cue(key == Key::l1 || key == Key::r1 ? Cue::page : Cue::focus);
        }
        return;
    }
    case Key::square:
    {
        // A game on download sources: into the download queue, or out of it.
        if (game != nullptr && game->remote)
        {
            queue_remote(*game);
            return;
        }
        // The Mods switch: all of the game's mods on or off at once. Their own switches (Game
        // settings > Mods) keep their state behind it.
        if (game == nullptr || game->title_id == 0 || game->mods == 0)
        {
            if (game != nullptr)
                cue(Cue::error);
            return;
        }
        Game &chosen = games_[static_cast<std::size_t>(library_.selected)];
        const bool saved = services_.set_mods_enabled(chosen.title_id, !chosen.mods_enabled);
        if (saved)
            count_mods(chosen, services_.mods(chosen.title_id));
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    case Key::triangle:
        if (game == nullptr)
            return;
        if (game->remote)
        {
            say(tr("Download the game to change its settings."), true);
            cue(Cue::error);
            return;
        }
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
        delete_armed_ = false;
        read_mods(games_[static_cast<std::size_t>(library_.selected)]);
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
        if (game->remote) { play_remote(*game); return; }
        request_launch(game->file, game->name, game->cover);
        return;
    case Key::options:
        library_sort_open_ = true;
        modal_elapsed_ = 0.0f;
        library_sort_choice_ = library_sort_;
        cue(Cue::modal_open);
        return;
    default:
        return;
    }
}

void Launcher::draw_library(Canvas &c)
{
    auto &l = c.list;
    float header_alpha = 1.0f, content_alpha = 1.0f;
    if (transition_.running) {
        const float frame = transition_.elapsed * 60.0f;
        if (screen_ == Screen::library) {
            header_alpha = stock_motion::library_header_in(std::max(0.0f,frame-27.0f))/255.0f;
            content_alpha = stock_motion::library_content_in(std::max(0.0f,frame-33.0f))/255.0f;
        } else {
            header_alpha = stock_motion::library_header_out(frame)/255.0f;
            content_alpha = stock_motion::library_content_out(frame)/255.0f;
        }
    }
    l.push_opacity(header_alpha);
    draw_frame(c,tr("All Software"),"");
    draw_pad(c,Pad::options,1574.0f,98.0f,32.0f);
    text(c,library_sort_ == 1 ? tr("By File Name") : tr("By Title"),1758.0f,baseline(69,62,28),28,theme::kText,Align::right);
    l.line(1784,87,1793,96,2.5f,theme::kText); l.line(1793,96,1802,87,2.5f,theme::kText);
    l.pop_opacity();
    l.push_opacity(content_alpha);
    const int count = static_cast<int>(games_.size());
    if (count == 0) {
        text(c,tr("No ROM files found."),960,500,32,theme::kCopy,Align::center);
        text_fit(c,services_.files_folder(),960,556,24,theme::kMeta,1400,Align::center);
    } else {
        const Rect grid_clip{120.0f,185.0f,1668.0f,767.0f};
        l.push_clip(grid_clip);
        const float scroll = library_scroll_.value;
        for (int i=0;i<count;++i) {
            Rect r{kLibraryTileX + (i%kLibraryColumns)*kLibraryTilePitch,
                   kLibraryTileY + (i/kLibraryColumns)*kLibraryTilePitch-scroll,kLibraryTileSize,kLibraryTileSize};
            if (r.y+r.h < grid_clip.y-16 || r.y > grid_clip.y+grid_clip.h+16) continue;
            const auto &game=games_[static_cast<std::size_t>(i)];
            if (game.remote) remote_cover(c,game.cover,r,0,1);
            else cover(c,game.cover,r,0,1);
            if (game.remote) {
                const Download *download = download_of(game.key);
                if (download != nullptr && download->total > 0 &&
                    (download->state == DownloadState::downloading || download->state == DownloadState::verifying)) {
                    const float amount = std::clamp(static_cast<float>(static_cast<double>(download->done) / download->total),0.0f,1.0f);
                    l.rounded_rect({r.x+12,r.y+r.h-18,r.w-24,6},3,theme::kRule);
                    if(amount>0) l.rounded_rect({r.x+12,r.y+r.h-18,std::max(6.0f,(r.w-24)*amount),6},3,theme::kLime);
                }
            }
            if (i == previous_library_focus_ && i != library_.selected && focus_elapsed_ < .05f)
                stock_focus(c,r,stock_motion::library_unfocus_alpha(focus_elapsed_*60)/255.0f,focus_elapsed_);
            if (i==library_.selected) {
                stock_focus(c,r,stock_motion::library_focus_alpha(focus_elapsed_*60)/255.0f,focus_elapsed_);
                if (!selected_game_.empty()) l.rounded_rect(r,0,Color::rgb(0xffffff,stock_motion::library_decide_flash(launch_.elapsed*60)/255.0f));
            }
        }
        const auto &game=games_[static_cast<std::size_t>(library_.selected)];
        const float card_x=kLibraryTileX+(library_.selected%6)*kLibraryTilePitch;
        const float card_y=kLibraryTileY+(library_.selected/6)*kLibraryTilePitch-scroll;
        const float label_width=std::clamp(text_width(c,game.name,30)+42.0f,180.0f,980.0f);
        const float label_x=std::clamp(card_x+kLibraryTileSize*.5f-label_width*.5f,112.0f,1808.0f-label_width);
        const Rect label{label_x,card_y-84.0f,label_width,65.0f};
        const Rect tip{card_x+(kLibraryTileSize-33.0f)*.5f,label.y+label.h-3.0f,33.0f,19.5f};
        const Color shadow=Color::rgb(0,c.dialog_shadow*.6f);
        l.shadow({label.x,label.y+1.5f,label.w,label.h},4.5f,6.0f,shadow);
        if(c.textures.stock_ui()) {
            // FlcBalloon mirrors each half texture across U=1. The pointer and
            // its authored shadow share the centre; the label covers their join.
            const auto mirrored=[&](stock_assets::Icon icon,const Rect &r,Color tint) {
                auto uv=stock_assets::icon_uv(icon);
                l.image(c.textures.stock_ui(),{r.x,r.y,r.w*.5f,r.h},uv,tint);
                uv.x+=uv.w; uv.w=-uv.w;
                l.image(c.textures.stock_ui(),{r.x+r.w*.5f,r.y,r.w*.5f,r.h},uv,tint);
            };
            mirrored(stock_assets::Icon::balloon_shadow,
                     {tip.x+1.5f,tip.y-3.75f,30.0f,27.0f},shadow);
            mirrored(stock_assets::Icon::balloon_tip,tip,theme::kShortcut);
        }
        glass(c,label,4.5f,theme::kShortcut,theme::kPanelEdge.with_alpha(0),0);
        text_marquee(c,game.name,{label.x+21,label.y,label.w-42,label.h},30,theme::kLime,focus_elapsed_,Align::center);
        l.pop_clip();
        const float total_height=((count+5)/6)*kLibraryTilePitch;
        if (total_height > grid_clip.h) {
            const float thumb_height=std::max(48.0f,grid_clip.h*grid_clip.h/total_height);
            const float max_scroll=std::max(1.0f,kLibraryTileY+((count+5)/6-1)*kLibraryTilePitch+kLibraryTileSize-940.0f);
            l.rounded_rect({1814,190,6,750},3,theme::kFaint.with_alpha(.22f));
            l.rounded_rect({1814,190+(750-thumb_height)*std::clamp(scroll/max_scroll,0.0f,1.0f),6,thumb_height},3,theme::kText.with_alpha(.58f));
        }
        if (!message_.empty()) {
            const Rect notification{112,192,900,76};
            glass(c,notification,0,theme::kShortcut,theme::kPanelEdge.with_alpha(0),1);
            notice(c,message_,134,baseline(notification.y,notification.h,22),22,message_warning_ ? theme::kWarning : theme::kText,856,message_warning_);
        }
    }
    l.pop_opacity();
    static constexpr Hint hints[]={{Pad::triangle,TR("Options")},{Pad::circle,TR("Back")},{Pad::cross,TR("Start")}};
    if (!library_sort_open_) {
        const Game *selected = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
        if (selected != nullptr && selected->remote) {
            static constexpr Hint remote[]={{Pad::square,TR("Download")},{Pad::circle,TR("Back")},{Pad::cross,TR("Download and play")}};
            static constexpr Hint queued[]={{Pad::square,TR("Cancel download")},{Pad::circle,TR("Back")},{Pad::cross,TR("Play when downloaded")}};
            draw_footer(c,download_of(selected->key) != nullptr ? queued : remote,3);
        } else draw_footer(c,hints,3);
    }
    if (library_sort_open_) {
        const float frame=modal_elapsed_*60.0f;
        l.rounded_rect({0,0,1920,1080},0,theme::kScrim.with_alpha(.65f*stock_motion::modal_mask_alpha(frame)/255.0f));
        l.push_opacity(stock_motion::modal_alpha(frame)/255.0f);
        l.push_transform(1+(stock_motion::modal_scale(frame)-1)*motion(),960,540,0,0);
        c.dialog_shadow=stock_motion::modal_shadow(frame);
        glass(c,{540,330,840,360},0,theme::kPanel,theme::kPanelEdge.with_alpha(0),1);
        text(c,tr("Sort Software"),582,392,36,theme::kTitle);
        const char *labels[]={TR("By Title"),TR("By File Name")};
        for(int i=0;i<2;++i) {
            Rect r{582,430.0f+96*i,756,94}; plate_rest(c,kRowPlate,r);
            if(i==library_sort_choice_) plate_focus(c,kRowPlate,r,1);
            text(c,tr(labels[i]),618,baseline(r.y,r.h,28),28,theme::kText);
            if(i==library_sort_ && c.textures.stock_ui())
                l.image(c.textures.stock_ui(),{1248,r.y+24,36,36},stock_assets::icon_uv(stock_assets::Icon::check),theme::kLime);
        }
        l.pop_transform();
        l.pop_opacity();
        static constexpr Hint sorting[]={{Pad::circle,TR("Back")},{Pad::cross,TR("OK")}};
        draw_footer(c,sorting,2);
    }
}

// ---------------------------------------------------------------- game settings dialog

int Launcher::game_row(int row) const
{
    if (row < row_save)
        return row;
    return services_.save_transfer_available() ? row : row + 1;
}

void Launcher::press_game(Key key)
{
    Game &game = games_[static_cast<std::size_t>(library_.selected)];
    const int kind = game_row(option_);
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
            delete_armed_ = false;
            cue(Cue::focus);
        }
        return;
    case Key::square:
        if (kind != row_save)
            return;
        break;
    case Key::left:
    case Key::right:
    case Key::cross:
        break;
    default:
        return;
    }
    if (kind >= row_video && kind <= row_language)
    {
        // A kind of setting has its own list.
        if (key == Key::cross)
            open_game_options(kind - row_video);
        return;
    }
    if (kind == row_mods)
    {
        // The game's mods have their own list. It is read again: mods may have been copied in
        // since this dialog opened.
        if (key != Key::cross)
            return;
        read_mods(game);
        mod_rows_.visible = kDialogRowsShown;
        mod_rows_.pitch = kDialogRowPitch;
        mod_rows_.reset(static_cast<int>(mod_list_.size()), 0);
        modal_ = modal_shown_ = Modal::mods;
        modal_elapsed_ = 0.0f;
        modal_closing_ = false;
        message_.clear();
        cue(Cue::open);
        return;
    }
    if (kind == row_delete)
    {
        // Deleting a game a download source has: its file and all of its updates and DLC go, its save
        // data and settings stay. Cross asks first, then deletes.
        if (key != Key::cross)
            return;
        if (!delete_armed_)
        {
            delete_armed_ = true;
            say(tr("Press again to delete the game and its updates and DLC from the console. Save data and "
                   "settings are kept."),
                true);
            cue(Cue::notify);
            return;
        }
        delete_armed_ = false;
        std::string result;
        if (!services_.delete_game(game, &result))
        {
            say(result.empty() ? std::string{tr("Could not delete the game.")} : result, true);
            cue(Cue::error);
            return;
        }
        // On its sources only now: so it shows until the list is read again (it keeps its place).
        game.remote = true;
        game.title_id = 0;
        game.addons.clear();
        game.addons_short.clear();
        game.mods = game.mods_on = 0;
        close_modal();
        read_home();
        refresh_selected_game();
        say(result);
        cue(Cue::saved);
        return;
    }
    if (kind == row_save)
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
    // The console mode.
    const bool saved = services_.set_docked(game.title_id, !game_docked_);
    if (saved)
        game_docked_ = !game_docked_;
    say(saved ? tr("Saved for this game. Applies on next launch.") : tr("Could not save. Please try again."),
        !saved);
    cue(saved ? Cue::toggle : Cue::error);
}

void Launcher::draw_game(Canvas &c, float open)
{
    auto &list=c.list;
    list.push_opacity(open);
    list.rounded_rect({0,0,1920,1080},0,theme::kBase);
    draw_frame(c,tr("Options"),"");
    const Game *game=games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    if(game) {
        cover(c,game->cover,{156,228,312,312},0,1);
        text_block(c,game->name,156,baseline(565,42,30),30,42,theme::kText,360,3);
        text(c,game->format+" \xC2\xB7 "+game->size,156,750,24,theme::kMeta);
        text_block(c,game->addons,156,798,22,32,theme::kMeta,360,3);
    }
    // A kind of setting: what the game does differently from Settings, if anything.
    const auto changed = [this](int category)
    {
        const int count = game_overrides(category);
        return count == 0 ? std::string{tr("Follows Settings")} :
                            fill(tr("{0} changed"), {std::to_string(count)});
    };
    const std::string values[] = {
        game_docked_ ? tr("Docked") : tr("Handheld"),
        changed(row_video - row_video),
        changed(row_performance - row_video),
        changed(row_audio - row_video),
        changed(row_controls - row_video),
        changed(row_system - row_video),
        changed(row_language - row_video),
        // With the game's Mods switch off (the Library's), none of them is on.
        mods_.empty() ? std::string{tr("No mods")} :
        game != nullptr && !game->mods_enabled ? std::string{tr("Off")} :
            fill(tr("{0} of {1} on"),
                 {std::to_string(std::count_if(mods_.begin(), mods_.end(),
                                               [](const Mod &mod) { return mod.enabled; })),
                  std::to_string(mods_.size())}),
        import_source_ == SaveSource::ryujinx ? tr("Ryujinx save found") :
        import_source_ == SaveSource::folder ? tr("Save folder found") : tr("Nothing to import"),
        game != nullptr ? game->size : std::string{},
    };
    static constexpr const char *kLabels[] = {TR("Console mode"), TR("Video"),    TR("Performance"),
                                              TR("Audio"),        TR("Controls"), TR("System"), TR("Language"),
                                              TR("Mods"),         TR("Save data"), TR("Delete from console")};
    const float top=228.0f;
    list.push_clip({586,top-6,1228,5*96.0f+12});
    for(int row=game_rows_.first_row();row<=game_rows_.last_row();++row) {
        const float y=top+row*96.0f-game_rows_.scroll();
        const Rect r{600,y,1182,94};
        plate_rest(c,kRowPlate,r);
        if(row==option_) plate_focus(c,kRowPlate,r,1);
        const int kind=game_row(row);
        const bool there=kind==row_mods ? !mods_.empty() && (game==nullptr || game->mods_enabled) :
            kind==row_save ? import_source_ != SaveSource::none : kind==row_delete ? false : game_overrides(kind-row_video)>0;
        const float taken=kind==row_mode ? chooser(c,values[kind],1746,baseline(y,94,28),row==option_ ? 1.0f : 0.0f,theme::kLime) :
            text_shrink(c,values[kind],1746,baseline(y,94,24),24,there ? theme::kLime : theme::kMeta,400,Align::right);
        text_shrink(c,tr(kLabels[kind]),636,baseline(y,94,28),28,kind==row_delete ? theme::kWarning : theme::kText,1110-taken-32);
    }
    list.pop_clip();
    scrollbar(c,game_rows_,1810,top,478);
    if(!message_.empty()) notice_block(c,message_,600,804,22,32,message_warning_ ? theme::kWarning : theme::kCopy,1182,3,message_warning_);
    else if(game_row(option_)==row_save) {
        text_block(c,tr("Import replaces the current save after confirmation. Export keeps a copy."),600,804,22,32,theme::kMeta,1182,2);
    }
    static constexpr Hint normal[]={{Pad::circle,TR("Back")},{Pad::cross,TR("Select")}};
    static constexpr Hint save[]={{Pad::square,TR("Export a copy")},{Pad::circle,TR("Back")},{Pad::cross,TR("Import")}};
    static constexpr Hint remove[]={{Pad::circle,TR("Back")},{Pad::cross,TR("Delete")}};
    const int kind=game_row(option_);
    draw_footer(c,kind==row_delete ? remove : kind==row_save ? save : normal,kind==row_save ? 3 : 2);
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
        modal_elapsed_ = 0.0f;
        modal_closing_ = false;
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
        const ModRow at = mod_list_[static_cast<std::size_t>(mod_rows_.selected)];
        Mod &mod = mods_[static_cast<std::size_t>(at.mod)];
        // With the game's Mods switch off (the Library's), choosing a mod turns the switch on and
        // that mod with it: nobody switches a mod in a list that is switched off.
        const bool revive = !game.mods_enabled;
        if (at.cheat >= 0)
        {
            // One of the mod's cheats. Choosing it turns on what it runs behind: its mod, and the
            // game's Mods switch. One of a group (two frame rates) takes the other's place, so
            // the list is read again.
            const Cheat &cheat = mod.cheats[static_cast<std::size_t>(at.cheat)];
            const bool enabled = revive || !mod.enabled || !cheat.enabled;
            const bool saved =
                (!revive || services_.set_mods_enabled(game.title_id, true)) &&
                (mod.enabled || services_.set_mod_enabled(game.title_id, mod.name, true)) &&
                services_.set_cheat_enabled(game.title_id, mod.name, cheat.name, enabled);
            read_mods(game);
            if (mod_rows_.count != static_cast<int>(mod_list_.size()))
                mod_rows_.reset(static_cast<int>(mod_list_.size()), 0);
            say(saved ? tr("Saved for this game. Applies on next launch.") :
                        tr("Could not save. Please try again."),
                !saved);
            cue(saved ? Cue::toggle : Cue::error);
            return;
        }
        const bool enabled = revive || !mod.enabled;
        const bool saved = (!revive || services_.set_mods_enabled(game.title_id, true)) &&
                           services_.set_mod_enabled(game.title_id, mod.name, enabled);
        if (saved)
            mod.enabled = enabled;
        count_mods(game, mods_);
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
    list.push_transform(1.0f, 960.0f, 540.0f, 0.0f,
                        0.0f);
    glass(c, {340.0f, 180.0f, 1240.0f, 720.0f}, 0.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);
    text_shrink(c, tr("Mods"), 382.0f, baseline(218.0f, 62.0f, theme::kDisplay), theme::kDisplay,
                theme::kTitle, 1156.0f);
    const Game *game = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    text_fit(c, game != nullptr ? game->name : std::string{}, 382.0f,
             baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall, Color::rgb(0xbdbdbd), 1156.0f);

    if (mods_.empty())
    {
        text_block(c,
                   fill(tr("No mods for this game yet. Copy each mod's folder to {0}, next to roms/."),
                        {game != nullptr ? services_.mods_folder(game->title_id) : std::string{}}),
                   382.0f, baseline(364.0f, 40.0f, theme::kText24), theme::kText24, 40.0f,
                   theme::kBody, 1156.0f, 5, kShrink);
    }
    else
    {
        list.push_clip({358.0f, kDialogWindow.y - 6.0f, 1204.0f,
                        kDialogWindow.h + 12.0f});
        const auto row_top = [&](int row)
        { return kDialogRowsTop + static_cast<float>(row) * kDialogRowPitch - mod_rows_.scroll(); };
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            plate_rest(c, kRowPlate, {382.0f, row_top(row), 1156.0f, kDialogRowHeight});
            list.pop_opacity();
        }
        plate_focus(c, kRowPlate,
                    {382.0f, kDialogRowsTop + mod_rows_.cursor() - mod_rows_.scroll(), 1156.0f,
                     kDialogRowHeight},
                    1.0f);
        // With the game's Mods switch off (the Library's) no mod is used: their own switches keep
        // their state, drawn faint.
        const bool live = game == nullptr || game->mods_enabled;
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            const ModRow at = mod_list_[static_cast<std::size_t>(row)];
            const Mod &mod = mods_[static_cast<std::size_t>(at.mod)];
            const float top = row_top(row);
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            if (at.cheat >= 0)
            {
                // One of the mod's cheats, set in under it, with its own switch: faint while its
                // mod is off.
                const Cheat &cheat = mod.cheats[static_cast<std::size_t>(at.cheat)];
                const bool used = live && mod.enabled;
                text_fit(c, cheat.name, 458.0f,
                         baseline(top + (kDialogRowHeight - 38.0f) * 0.5f, 38.0f, theme::kText24),
                         theme::kText24, cheat.enabled && used ? theme::kValue : theme::kMeta, 520.0f);
                list.push_opacity(used ? 1.0f : 0.4f);
                toggle(c, 1502.0f, top + kDialogRowHeight * 0.5f, cheat.enabled ? 1.0f : 0.0f);
                list.pop_opacity();
                list.pop_opacity();
                continue;
            }
            // Its name as the player's folder has it, what it changes under it (with how many of
            // its cheats are chosen when it lists several), its switch.
            std::string kind = mod.kind;
            if (!mod.cheats.empty())
                kind += ", " + fill(tr("{0} of {1} on"),
                                    {std::to_string(std::count_if(
                                         mod.cheats.begin(), mod.cheats.end(),
                                         [](const Cheat &cheat) { return cheat.enabled; })),
                                     std::to_string(mod.cheats.size())});
            text_fit(c, mod.name, 418.0f, baseline(top + 14.0f, 38.0f, theme::kText24),
                     theme::kText24, mod.enabled && live ? theme::kValue : theme::kMeta, 560.0f);
            text_shrink(c, kind, 418.0f, baseline(top + 52.0f, 28.0f, theme::kSmall),
                        theme::kSmall, theme::kMeta, 560.0f);
            list.push_opacity(live ? 1.0f : 0.4f);
            toggle(c, 1502.0f, top + kDialogRowHeight * 0.5f, mod.enabled ? 1.0f : 0.0f);
            list.pop_opacity();
            list.pop_opacity();
        }
        list.pop_clip();
        scrollbar(c, mod_rows_, 1550.0f, kDialogWindow.y, kDialogWindow.h);
    }

    if (!message_.empty())
    {
        notice_block(c, message_, 382.0f, kDialogHints - 6.0f, theme::kSmall, 26.0f,
                     message_warning_ ? theme::kWarning : theme::kLimePale, 1156.0f, 2,
                     message_warning_);
    }
    else if (mods_.empty())
    {
        static constexpr Hint kEmpty[] = {{Pad::square, TR("Create the folder")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kEmpty, 2, 382.0f, kDialogHints, theme::kCopy, 1156.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {{Pad::updown, TR("Select")},
                                          {Pad::cross, TR("Turn on or off")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 382.0f, kDialogHints, theme::kCopy, 1156.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

} // namespace pe::ui
