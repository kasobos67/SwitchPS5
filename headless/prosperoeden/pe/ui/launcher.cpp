// ProsperoEden - The launcher: home, library, settings and their dialogs.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"
#include "pe/ui/stock_motion.hpp"

#include <algorithm>

namespace pe::ui
{

using audio::Cue;

namespace
{
// How long the notification of a newer release stays.
constexpr float kUpdateNoticeSeconds = 10.0f;
// The update dialog's full height (update.cpp).
constexpr float kUpdatePanelHeight = 688.0f;
} // namespace

namespace
{

constexpr Rect kScreen{0.0f, 0.0f, 1920.0f, 1080.0f};

} // namespace

Launcher::Launcher(Services &services, Textures &textures, const Fonts &fonts, bool first_start,
                   std::string presentation_file)
    : services_(services), textures_(textures), fonts_(fonts), first_start_(first_start)
{
    version_ = services_.version();
    clock_ = services_.clock();
    room_status_ = services_.room_status();
    prefs_ = services_.preferences();
    presentation_file_ = presentation_file.empty() ? services_.files_folder() + "/.switch-ui.cfg" : std::move(presentation_file);
    presentation_.load(presentation_file_);
    sources_ = services_.sources();
    sources_generation_ = sources_.generation;
    downloads_ = services_.downloads();
    apply_look();
    read_profiles();
    read_home();
    const bool continue_ready = home_.setup_ready && !home_tiles_.empty();
    home_focus_ = continue_ready ? 0 : home_.setup_ready ? kHomeAllSoftware : 5;
    home_springs_[static_cast<std::size_t>(home_focus_)].snap(1.0f);
    settings_.visible = 10;
    settings_.pitch = 68.0f;
    settings_.reset(13, 0);
    section_.snap(1.0f);
    detail_.snap(1.0f);
    cue(home_.launch_failed ? Cue::notify : first_start ? Cue::welcome : Cue::resume);
    start_scan();
}

Launcher::~Launcher()
{
    seed_edit_stop_ = true;
    if (seed_edit_.valid()) seed_edit_.wait();
    room_directory_stop_ = true;
    if (room_directory_request_.valid()) room_directory_request_.wait();
    room_edit_stop_ = true;
    if (room_edit_.valid()) room_edit_.wait();
    // The game list may still be reading; it uses the services this launcher was given.
    if (scan_.valid())
        scan_.wait();
}

std::vector<Cue> Launcher::take_cues()
{
    std::vector<Cue> result;
    result.swap(cues_);
    return result;
}

void Launcher::cue(Cue value)
{
    if (value == Cue::focus)
    {
        value = modal_ == Modal::profiles || home_profile_focus_ ? Cue::focus_account :
                screen_ == Screen::home ? (home_focus_ >= 1 && home_focus_ <= 6 ? Cue::focus_applet :
                    home_focus_ == kHomeAllSoftware ? Cue::focus_library_button : Cue::focus) :
                screen_ == Screen::library && modal_ == Modal::none ? Cue::focus :
                screen_ == Screen::settings && !settings_content_ ? Cue::focus_navigation : Cue::focus_menu;
    }
    if (value == Cue::open && leaving_ == Screen::home && screen_ == Screen::about) value = Cue::news_open;
    if (value == Cue::open && leaving_ == Screen::home && screen_ == Screen::settings && home_focus_ == 4) value = Cue::controllers_open;
    if (value == Cue::modal_open && modal_ == Modal::profiles && !profile_for_launch_) value = Cue::profile_open;
    if (value == Cue::launch && screen_ == Screen::library) value = Cue::library_launch;
    cues_.push_back(value);
}

void Launcher::say(const std::string &text, bool warning)
{
    message_ = text;
    message_warning_ = warning;
    message_age_ = 0.0f;
}

void Launcher::open(Screen screen, bool forward)
{
    leaving_ = screen_;
    screen_ = screen;
    forward_ = forward;
    transition_.start(0.65f);
    message_.clear();
    cue(!forward ? Cue::back : screen == Screen::library ? Cue::library_open : Cue::open);
}

void Launcher::open_modal(Modal modal)
{
    modal_ = modal_shown_ = modal;
    modal_elapsed_ = 0.0f;
    modal_closing_ = false;
    option_ = 0;
    option_cursor_.snap(dialog_row_top(modal, 0));
    message_.clear();
    // Switches show their state at once; they only animate when changed.
    const std::array<bool, 7> states = switch_states(modal);
    for (std::size_t i = 0; i < states.size(); ++i)
        switches_[i].snap(states[i] ? 1.0f : 0.0f);
    cue(Cue::modal_open);
}

std::array<bool, 7> Launcher::switch_states(Modal modal) const
{
    switch (modal)
    {
    case Modal::video:
        return {prefs_.hud, false, false};
    case Modal::performance:
        return {prefs_.block_list,  prefs_.async_shaders,     prefs_.fast_gpu, prefs_.unsafe_cpu,
                prefs_.unsafe_dma,  prefs_.reactive_flushing, prefs_.skip_invalidation};
    case Modal::audio:
        return {prefs_.mute, false, false};
    case Modal::controls:
        return {prefs_.vibration, false, false};
    case Modal::accessibility:
        return {prefs_.large_text, prefs_.high_contrast, prefs_.reduce_motion};
    default:
        return {prefs_.detailed_logging, prefs_.immediate_logs, false};
    }
}

void Launcher::close_modal()
{
    modal_ = Modal::none;
    modal_elapsed_ = 0.0f;
    modal_closing_ = true;
    message_.clear();
    cue(Cue::modal_close);
}

void Launcher::apply_look()
{
    look() = {prefs_.large_text, prefs_.high_contrast, prefs_.reduce_motion, presentation_.white_theme};
    theme::set_white(presentation_.white_theme);
}

bool Launcher::set_ui_theme(bool white)
{
    const auto previous = presentation_;
    presentation_.white_theme = white;
    if (!presentation_.save(presentation_file_)) { presentation_ = previous; say(tr("Could not save settings. Please try again."), true); cue(Cue::error); return false; }
    apply_look();
    return true;
}

bool Launcher::set_profile_avatar(const std::string &name, int icon, int color)
{
    const auto previous = presentation_;
    presentation_.set_avatar(name, icon, color);
    if (!presentation_.save(presentation_file_)) { presentation_ = previous; say(tr("Could not save settings. Please try again."), true); cue(Cue::error); return false; }
    return true;
}

void Launcher::request_launch(const std::string &file, const std::string &title, const std::string &cover)
{
    pending_launch_ = {file, title, cover};
    read_profiles();
    // The account selected through Services is already the current user.
    // Keep the existing game handoff and skip the per-launch account chooser.
    launch_pending();
}

void Launcher::launch_pending()
{
    const auto game = pending_launch_;
    pending_launch_ = {};
    if (game.file.empty()) return;
    launch(game.file, game.title, game.cover);
}

bool Launcher::save_preferences(bool quiet)
{
    const bool saved = services_.set_preferences(prefs_);
    if (!saved)
        say(tr("Could not save settings. Please try again."), true);
    else if (quiet)
        message_.clear();
    else if (modal_ == Modal::audio && option_ == 2)
        say(tr("Saved. Menu sound level changed."));
    else
        say(tr("Saved. Applies when a game starts."));
    return saved;
}

void Launcher::launch(const std::string &file, const std::string &title, const std::string &cover)
{
    // A game whose file was taken away since the menu showed it is not started: it leaves the
    // menu instead.
    if (!services_.game_exists(file))
    {
        read_home();
        drop_missing_games();
        say(tr("ROM missing from the game files folder"), true);
        cue(Cue::error);
        return;
    }
    // Its save data first, when the profile keeps it on a server.
    if (services_.save_sync_wanted(file))
    {
        sync_file_ = file;
        sync_title_ = title;
        sync_cover_ = cover;
        sync_launch_ = true;
        sync_started_ = false;
        sync_time_ = 0.0f;
        save_sync_wait_ = 1.0f; // looked at in this frame's update
        modal_ = modal_shown_ = Modal::save_sync;
        message_.clear();
        cue(Cue::modal_open);
        return;
    }
    start_game(file, title, cover);
}

void Launcher::start_game(const std::string &file, const std::string &title, const std::string &cover)
{
    if (!services_.can_launch_game()) {
        say(tr("Join a room successfully or leave it before starting a game."), true);
        cue(Cue::error);
        return;
    }
    services_.will_play(file);
    selected_game_ = services_.game_path(file);
    launch_title_ = title;
    launch_cover_ = cover;
    launch_.start(theme::kLaunchSeconds);
    press_ = 1.0f;
    cue(Cue::launch);
    if (!selected_game_.empty()) {
        presentation_.remember(file);
        presentation_.save(presentation_file_);
        if (screen_ == Screen::home) cue(Cue::launch_effect);
    }
}

void Launcher::press(Key key)
{
    if (screen_off_) { screen_off_ = false; cue(Cue::resume); return; }
    if (!selected_game_.empty())
        return; // a game is starting
    if (modal_ == Modal::game)
        return press_game(key);
    if (modal_ == Modal::mods)
        return press_mods(key);
    if (modal_ == Modal::game_options)
        return press_game_options(key);
    if (modal_ == Modal::mapping)
        return press_mapping(key);
    if (modal_ == Modal::profiles)
        return press_profiles(key);
    if (modal_ == Modal::update)
        return press_update(key);
    if (modal_ == Modal::settings_choice)
        return press_settings_choice(key);
    if (modal_ == Modal::download) return press_download(key);
    if (modal_ == Modal::source) return press_choice(key);
    if (modal_ == Modal::sources) return press_sources(key);
    if (modal_ == Modal::save_sync) return press_save_sync(key);
    if (modal_ == Modal::multiplayer) return press_multiplayer(key);
    if (modal_ == Modal::sync_setup) return press_sync_setup(key);
    if (modal_ == Modal::pairing) return press_pairing(key);
    if (modal_ != Modal::none)
        return press_dialog(key);
    switch (screen_)
    {
    case Screen::home:
        return press_home(key);
    case Screen::library:
        return press_library(key);
    case Screen::settings:
        return press_settings(key);
    case Screen::files:
        return press_files(key);
    case Screen::language:
        return press_language(key);
    case Screen::about:
        if (key == Key::circle)
            open(Screen::home, false);
        return;
    }
}

void Launcher::update(float dt)
{
    time_ += dt;
    intro_ += dt;
    focus_elapsed_ += dt;
    modal_elapsed_ += dt;
    message_age_ += dt;
    backdrop_.update(dt);
    textures_.pump(dt);
    finish_scan(false);
    poll_sources(dt);
    poll_save_sync(dt);
    poll_pairing(dt);
    refresh_sync_setup(dt);
    update_multiplayer();
    update_game_options();
    update_controllers(dt);
    transition_.update(dt);
    press_ = std::max(0.0f, press_ - dt / 0.18f);

    modal_open_.target = modal_ != Modal::none ? 1.0f : 0.0f;
    modal_open_.update(dt, 20.0f);
    if (modal_ == Modal::none && modal_elapsed_ >= 0.4f)
        modal_shown_ = Modal::none;
    dim_.target = screen_ == Screen::home ? 0.0f : 0.30f;
    dim_.update(dt, 8.0f);

    for (std::size_t i = 0; i < home_springs_.size(); ++i)
    {
        home_springs_[i].target = static_cast<int>(i) == home_focus_ ? 1.0f : 0.0f;
        home_springs_[i].update(dt, theme::kFocusSpring);
    }
    const int home_recents = home_recent_count();
    if (home_focus_ == 0 || home_focus_ >= kHomeFirstRecent)
    {
        const int tile = home_focus_ == 0 ? 0 : home_focus_ == kHomeAllSoftware ? home_recents + 1 : home_focus_ - 6;
        home_strip_.target = kHomeTilePitch * std::max(0, tile - 3);
    }
    if (look().reduce_motion)
        home_strip_.snap(home_strip_.target);
    else
        home_strip_.update(dt, 24.0f);
    library_.update(dt);
    const float selected_top = kLibraryTileY + (library_.selected / kLibraryColumns) * kLibraryTilePitch;
    const float max_scroll = std::max(0.0f, kLibraryTileY + ((static_cast<int>(games_.size()) + 5) / 6 - 1) * kLibraryTilePitch + kLibraryTileSize - 940.0f);
    if (selected_top - library_scroll_.target < 250.0f) library_scroll_.target = std::max(0.0f, selected_top - 250.0f);
    if (selected_top + kLibraryTileSize - library_scroll_.target > 940.0f) library_scroll_.target = std::min(max_scroll, selected_top + kLibraryTileSize - 940.0f);
    if (look().reduce_motion) library_scroll_.snap(library_scroll_.target);
    else library_scroll_.update(dt, 24.0f);
    setting_choice_rows_.update(dt);
    settings_.update(dt);
    files_.update(dt);
    language_.update(dt);
    video_rows_.update(dt);
    hud_rows_.update(dt);
    performance_rows_.update(dt);
    game_rows_.update(dt);
    mod_rows_.update(dt);
    option_rows_.update(dt);
    mapping_rows_.update(dt);
    profile_rows_.update(dt);
    source_rows_.update(dt);
    choice_rows_.update(dt);
    sync_rows_.update(dt);
    sync_setup_rows_.update(dt);
    mode_.target = selected_docked_ ? 0.0f : 1.0f;
    mode_.update(dt, 22.0f);
    const bool mods_on = library_.selected >= 0 && library_.selected < static_cast<int>(games_.size()) &&
                         games_[static_cast<std::size_t>(library_.selected)].mods_enabled;
    mods_switch_.target = mods_on ? 1.0f : 0.0f;
    mods_switch_.update(dt, 22.0f);
    detail_.target = 1.0f;
    detail_.update(dt, 14.0f);
    section_.target = 1.0f;
    section_.update(dt, 14.0f);
    if (screen_ == Screen::settings && settings_content_ && modal_ == Modal::none)
        option_cursor_.target = dialog_row_top(settings_modal(), option_);
    else if (modal_ != Modal::none)
        option_cursor_.target = dialog_row_top(modal_, option_);
    option_cursor_.update(dt, theme::kCursorSpring);
    const std::array<bool, 7> states = switch_states(modal_shown_);
    for (std::size_t i = 0; i < states.size(); ++i)
    {
        switches_[i].target = states[i] ? 1.0f : 0.0f;
        switches_[i].update(dt, 22.0f);
    }

    // Games removed from the game files folder while the menu is open leave it within a moment.
    presence_wait_ += dt;
    if (presence_wait_ >= 2.0f && selected_game_.empty())
    {
        presence_wait_ = 0.0f;
        check_games_present();
    }

    // A newer release: the update dialog, or a notification when the app cannot install it.
    update_offer(dt);
    update_install(dt);
    if (update_notice_left_ > 0.0f)
        update_notice_left_ = std::max(0.0f, update_notice_left_ - dt);
    // It slides away over its last moments.
    update_notice_in_.target = update_notice_left_ > 0.4f ? 1.0f : 0.0f;
    update_notice_in_.update(dt, 14.0f);

    clock_wait_ += dt;
    if (clock_wait_ >= 1.0f)
    {
        clock_wait_ = 0.0f;
        clock_ = services_.clock();
        // A room can disconnect after its menu closes. Keep HOME's status
        // indicator tied to the live multiplayer service, not the last menu.
        if (modal_ != Modal::multiplayer)
            room_status_ = services_.room_status();
    }

    if (!selected_game_.empty())
    {
        launch_.update(dt);
        if (!launch_.running)
            done_ = true;
    }
}

void Launcher::update_offer(float dt)
{
    (void)dt;
    if (!update_waiting_ && update_notice_left_ <= 0.0f && selected_game_.empty() && update_.version.empty())
    {
        UpdateOffer offer;
        if (services_.take_update(&offer) && !offer.version.empty())
        {
            // "v1.000.050" and "1.000.050" both read as the number.
            const std::string &newer = offer.version;
            update_version_ = newer.size() > 1 && (newer[0] == 'v' || newer[0] == 'V') ? newer.substr(1) : newer;
            if (offer.installable && first_start_)
            {
                // Asked each time the app opens (not on returning from a game).
                update_ = offer;
                notes_laid_out_ = false;
                update_waiting_ = true;
            }
            else
            {
                update_notice_left_ = kUpdateNoticeSeconds;
                cue(Cue::notify);
            }
        }
    }
    // The dialog waits for the welcome and for whatever dialog is open to close.
    if (update_waiting_ && modal_ == Modal::none && modal_shown_ == Modal::none && !transition_.running &&
        selected_game_.empty() && intro_ > 1.2f)
    {
        update_waiting_ = false;
        update_stage_ = UpdateStage::offer;
        update_stage_time_ = 0.0f;
        update_choice_ = 0;
        update_choice_x_.snap(0.0f);
        update_height_.snap(kUpdatePanelHeight);
        modal_ = modal_shown_ = Modal::update;
        modal_elapsed_ = 0.0f;
        modal_closing_ = false;
        message_.clear();
        cue(Cue::notify);
    }
}

void Launcher::draw_screen(Canvas &c, Screen screen)
{
    switch (screen)
    {
    case Screen::home:
        return draw_home(c);
    case Screen::library:
        return draw_library(c);
    case Screen::settings:
        return draw_settings(c);
    case Screen::files:
        return draw_files(c);
    case Screen::language:
        return draw_language(c);
    case Screen::about:
        return draw_about(c);
    }
}

void Launcher::draw_frame(Canvas &c, const char *title, const char *copy)
{
(void)copy;
    home_symbol(c, std::string{title} == tr("All Software") ? 0 :
                   std::string{title} == tr("About Switch") ? 2 : 1, 126.0f, 100.0f, theme::kText);
    text_shrink(c, title, 178.0f, baseline(66.0f,68.0f,40.0f), 40.0f, theme::kTitle, 1430.0f);
    c.list.line(96.0f,157.0f,1824.0f,157.0f,1.0f,theme::kRule);
}

void Launcher::draw_footer(Canvas &c, const Hint *hints, int count)
{
    if (!draw_chrome_) return;
c.list.line(45.0f,971.0f,1875.0f,971.0f,1.5f,theme::kRule);
    draw_controllers(c);
    draw_hints_right(c,hints,count,1836.0f,1025.0f,theme::kText);
}

void Launcher::draw_update_notice(Canvas &c)
{
    if(update_notice_left_<=0 || update_version_.empty()) return;
    const float age=kUpdateNoticeSeconds-update_notice_left_;
    const float alpha=std::min(stock_motion::screen_in(age*60.0f)/255.0f,
        stock_motion::screen_in(update_notice_left_*60.0f)/255.0f);
    const Rect panel{30,24,666,114};
    c.list.push_opacity(alpha);
    c.list.shadow({panel.x,panel.y+1.5f,panel.w,panel.h},0,6,Color::rgb(0,.6f));
    c.list.rounded_rect(panel,0,Color::rgb(0x1c1c1c,.96f));
    if(c.textures.brand()) c.list.image(c.textures.brand(),{42,36,90,90},{0,0,1,1},Color::rgb(0xffffff));
    text(c,tr("Update available"),156,71,28,Color::rgb(0xffffff));
    text_fit(c,fill(tr("Version {0} is on homebrew.page"),{update_version_}),156,110,24,Color::rgb(0xe7e7e7),510);
    c.list.pop_opacity();
}

void Launcher::draw_launch(Canvas &c)
{
    const float t = launch_.progress();
    // Confirm the selected tile in place, then fade into the renderer's black boot screen.
    // This only draws: launch() still owns the original timer and emulator handoff.
    const float black = tween::cubic_in_out((t - 0.12f) / 0.50f);
    c.list.rounded_rect(kScreen, 0.0f, Color::rgb(0x000000, black));
}

void Launcher::draw(gfx::DrawList &list)
{
    Canvas c{list, fonts_, textures_, backdrop_, time_};
    backdrop_.draw(list, textures_, dim_.value);
    const bool launching = !selected_game_.empty();
    if (screen_off_) { list.rounded_rect(kScreen,0,Color::rgb(0)); return; }
    // The profile page owns the whole screen. Do not leave a library or HOME
    // frame underneath its entrance/exit fade.
    const bool profile_page = modal_shown_ == Modal::profiles && !profile_for_launch_;
    if (profile_page)
    {
        draw_chrome_ = true;
    }
    else if (transition_.running)
    {
        const float frame=transition_.elapsed*60.0f;
        draw_chrome_=false;
        if (leaving_ == Screen::home && screen_ == Screen::library) {
            if (frame < 27.0f) draw_home(c);
            else draw_library(c);
        } else if (leaving_ == Screen::library && screen_ == Screen::home) {
            if (frame < 6.0f) draw_library(c);
            else draw_home(c);
        } else {
            // Page content cannot overlap; lower chrome persists through the two phases.
            const float half=transition_.duration*.5f;
            if (transition_.elapsed < half) {
                list.push_opacity(stock_motion::screen_out(transition_.elapsed/half*12.0f)/255.0f);
                draw_screen(c,leaving_); list.pop_opacity();
            } else {
                list.push_opacity(stock_motion::screen_in((transition_.elapsed-half)/half*8.0f)/255.0f);
                draw_screen(c,screen_); list.pop_opacity();
            }
        }
        draw_chrome_=true;
        static constexpr Hint home_hints[]={{Pad::triangle,TR("Game settings")},{Pad::cross,TR("Select")}};
        static constexpr Hint library_hints[]={{Pad::triangle,TR("Options")},{Pad::circle,TR("Back")},{Pad::cross,TR("Start")}};
        static constexpr Hint page_hints[]={{Pad::circle,TR("Back")},{Pad::cross,TR("Select")}};
        if(modal_shown_ == Modal::none) {
            if(screen_==Screen::home) draw_footer(c,home_hints,2);
            else if(screen_==Screen::library) draw_footer(c,library_hints,3);
            else draw_footer(c,page_hints,2);
        }
    }
    else {
        draw_chrome_ = modal_shown_ == Modal::none;
        draw_screen(c,screen_);
        draw_chrome_ = true;
    }
    if (modal_shown_ != Modal::none)
    {
        const float frame=modal_elapsed_*60.0f;
        const float opened=(modal_closing_ ? stock_motion::modal_out_alpha(frame) : stock_motion::modal_alpha(frame))/255.0f;
        const bool settings_page = modal_shown_ == Modal::hud || modal_shown_ == Modal::video || modal_shown_ == Modal::performance ||
            modal_shown_ == Modal::audio || modal_shown_ == Modal::controls ||
            modal_shown_ == Modal::accessibility || modal_shown_ == Modal::diagnostics;
        const bool full_page = modal_shown_ == Modal::game || modal_shown_ == Modal::game_options ||
                               (modal_shown_ == Modal::profiles && !profile_for_launch_) || modal_shown_ == Modal::mapping;
        c.dialog_shadow = modal_closing_ ? stock_motion::modal_out_shadow(frame) : stock_motion::modal_shadow(frame);
        if (!settings_page && !full_page) {
            const float mask=(modal_closing_ ? stock_motion::modal_out_mask_alpha(frame) : stock_motion::modal_mask_alpha(frame))/255.0f;
            list.rounded_rect(kScreen,0,theme::kScrim.with_alpha(mask*.65f));
        }
        const float modal_scale = settings_page || full_page ? 1.0f : 1.0f+
            ((modal_closing_ ? stock_motion::modal_out_scale(frame) : stock_motion::modal_scale(frame))-1.0f)*motion();
        list.push_transform(modal_scale,960,540,0,0);
        if (modal_shown_ == Modal::game) draw_game(c,opened);
        else if (modal_shown_ == Modal::mods) draw_mods(c,opened);
        else if (modal_shown_ == Modal::game_options) draw_game_options(c,opened);
        else if (modal_shown_ == Modal::mapping) draw_mapping(c,opened);
        else if (modal_shown_ == Modal::profiles) draw_profiles(c,opened);
        else if (modal_shown_ == Modal::settings_choice) draw_settings_choice(c,opened);
        else if (modal_shown_ == Modal::update) draw_update(c,opened);
        else if (modal_shown_ == Modal::download) draw_download(c,opened);
        else if (modal_shown_ == Modal::source) draw_choice(c,opened);
        else if (modal_shown_ == Modal::sources) draw_sources(c,opened);
        else if (modal_shown_ == Modal::save_sync) draw_save_sync(c,opened);
        else if (modal_shown_ == Modal::multiplayer) draw_multiplayer(c,opened);
        else if (modal_shown_ == Modal::sync_setup) draw_sync_setup(c,opened);
        else if (modal_shown_ == Modal::pairing) draw_pairing(c,opened);
        else if (modal_shown_ == Modal::hud) {
            list.rounded_rect({700.0f,196.0f,1136.0f,721.0f},0.0f,theme::kBase);
            draw_dialog(c,modal_shown_,opened);
            static constexpr Hint hints[] = {{Pad::updown,TR("Select")},
                {Pad::leftright,TR("Change")},{Pad::circle,TR("Back")}};
            draw_footer(c,hints,3);
        }
        else draw_dialog(c,modal_shown_,opened);
        list.pop_transform();
    }
    draw_update_notice(c);
    draw_save_sync_notice(c);
    if (launching) draw_launch(c);
    if (modal_shown_ == Modal::update && update_stage_ == UpdateStage::closing) {
        const float dark=tween::cubic_in_out((update_stage_time_-2.2f)/.8f);
        if(dark>0) list.rounded_rect(kScreen,0,Color::rgb(0,dark));
    }
}

} // namespace pe::ui
