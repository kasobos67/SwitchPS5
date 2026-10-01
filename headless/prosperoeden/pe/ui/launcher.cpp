// ProsperoEden - The launcher: home, library, settings and their dialogs.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include <algorithm>

namespace pe::ui
{

using audio::Cue;

namespace
{

constexpr Rect kScreen{0.0f, 0.0f, 1920.0f, 1080.0f};

} // namespace

Launcher::Launcher(Services &services, Textures &textures, const Fonts &fonts, bool first_start)
    : services_(services), textures_(textures), fonts_(fonts), first_start_(first_start)
{
    version_ = services_.version();
    clock_ = services_.clock();
    prefs_ = services_.preferences();
    home_ = services_.home();
    const bool continue_ready = home_.setup_ready && home_.last_exists;
    home_focus_ = continue_ready ? 0 : home_.setup_ready ? 1 : 2;
    home_springs_[static_cast<std::size_t>(home_focus_)].snap(1.0f);
    settings_.visible = 6;
    settings_.pitch = 102.0f;
    settings_.reset(6, 0);
    section_.snap(1.0f);
    detail_.snap(1.0f);
    cue(home_.launch_failed ? Cue::notify : first_start ? Cue::welcome : Cue::resume);
    start_scan();
}

Launcher::~Launcher()
{
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
    transition_.start(theme::kScreenSeconds);
    message_.clear();
    cue(forward ? Cue::open : Cue::back);
}

void Launcher::open_modal(Modal modal)
{
    modal_ = modal_shown_ = modal;
    option_ = 0;
    option_cursor_.snap(dialog_row_top(modal, 0));
    message_.clear();
    // Switches show their state at once; they only animate when changed.
    const bool states[] = {modal == Modal::video ? prefs_.hud :
                           modal == Modal::audio ? prefs_.mute :
                           modal == Modal::controls ? prefs_.vibration : prefs_.detailed_logging};
    switches_[0].snap(states[0] ? 1.0f : 0.0f);
    cue(Cue::modal_open);
}

void Launcher::close_modal()
{
    modal_ = Modal::none;
    message_.clear();
    cue(Cue::modal_close);
}

bool Launcher::save_preferences()
{
    const bool saved = services_.set_preferences(prefs_);
    say(saved ? "Saved. Applies when a game starts." : "Could not save settings. Please try again.",
        !saved);
    return saved;
}

void Launcher::launch(const std::string &file, const std::string &title, const std::string &cover)
{
    selected_game_ = services_.game_path(file);
    launch_title_ = title;
    launch_cover_ = cover;
    launch_.start(theme::kLaunchSeconds);
    press_ = 1.0f;
    cue(Cue::launch);
}

void Launcher::press(Key key)
{
    if (!selected_game_.empty())
        return; // a game is starting
    if (modal_ == Modal::game)
        return press_game(key);
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
    message_age_ += dt;
    backdrop_.update(dt);
    textures_.pump(dt);
    finish_scan(false);
    update_controllers(dt);
    transition_.update(dt);
    press_ = std::max(0.0f, press_ - dt / 0.18f);

    modal_open_.target = modal_ != Modal::none ? 1.0f : 0.0f;
    modal_open_.update(dt, 20.0f);
    if (modal_ == Modal::none && modal_open_.value < 0.01f)
        modal_shown_ = Modal::none;
    dim_.target = screen_ == Screen::home ? 0.0f : 0.30f;
    dim_.update(dt, 8.0f);

    for (std::size_t i = 0; i < home_springs_.size(); ++i)
    {
        home_springs_[i].target = static_cast<int>(i) == home_focus_ ? 1.0f : 0.0f;
        home_springs_[i].update(dt, theme::kFocusSpring);
    }
    library_.update(dt);
    settings_.update(dt);
    files_.update(dt);
    language_.update(dt);
    mode_.target = selected_docked_ ? 0.0f : 1.0f;
    mode_.update(dt, 22.0f);
    detail_.target = 1.0f;
    detail_.update(dt, 14.0f);
    section_.target = 1.0f;
    section_.update(dt, 14.0f);
    if (modal_ != Modal::none)
        option_cursor_.target = dialog_row_top(modal_, option_);
    option_cursor_.update(dt, theme::kCursorSpring);
    switches_[0].target = modal_shown_ == Modal::video ? (prefs_.hud ? 1.0f : 0.0f) :
                          modal_shown_ == Modal::audio ? (prefs_.mute ? 1.0f : 0.0f) :
                          modal_shown_ == Modal::controls ? (prefs_.vibration ? 1.0f : 0.0f) :
                          (prefs_.detailed_logging ? 1.0f : 0.0f);
    switches_[0].update(dt, 22.0f);

    clock_wait_ += dt;
    if (clock_wait_ >= 1.0f)
    {
        clock_wait_ = 0.0f;
        clock_ = services_.clock();
    }

    if (!selected_game_.empty())
    {
        launch_.update(dt);
        if (!launch_.running)
            done_ = true;
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
    text(c, title, 108.0f, baseline(62.0f, 64.0f, theme::kDisplay), theme::kDisplay, theme::kTitle);
    text(c, copy, 110.0f, baseline(130.0f, 30.0f, theme::kSmall), theme::kSmall, theme::kCopy);
}

void Launcher::draw_footer(Canvas &c, const Hint *hints, int count)
{
    c.list.rounded_rect({108.0f, 955.0f, 1704.0f, 1.0f}, 0.0f, Color::rgb(0x586d5a, 0.9f));
    draw_hints(c, hints, count, 108.0f, 987.0f, theme::kCopy);
}

void Launcher::draw_launch(Canvas &c)
{
    const float t = launch_.progress();
    // The menu dims, the game's cover steps forward, then everything goes dark.
    const float black = tween::cubic_in_out((t - 0.45f) / 0.55f);
    const float veil = std::max(0.90f * tween::cubic_out(t / 0.22f), black);
    c.list.rounded_rect(kScreen, 0.0f, Color::rgb(0x020705, veil));

    const float appear = tween::back_out(t / 0.38f);
    const float leave = 1.0f - tween::cubic_in_out((t - 0.60f) / 0.40f);
    c.list.push_opacity(tween::clamp01(t / 0.18f) * leave);
    c.list.push_transform(0.86f + 0.14f * appear, 960.0f, 470.0f, 0.0f, 0.0f);
    const Rect art{810.0f, 300.0f, 300.0f, 300.0f};
    c.list.shadow({art.x - 10.0f, art.y - 4.0f, art.w + 20.0f, art.h + 20.0f}, 30.0f, 70.0f,
                  theme::kLime.with_alpha(0.22f));
    cover(c, launch_cover_, art, 20.0f, 1.0f);
    text(c, "STARTING", 960.0f, 668.0f, theme::kSmall, theme::kLime, Align::center, 4.0f);
    text_fit(c, launch_title_, 960.0f, 716.0f, theme::kHeading, theme::kTitle, 1300.0f,
             Align::center);
    c.list.pop_transform();
    c.list.pop_opacity();
}

void Launcher::draw(gfx::DrawList &list)
{
    Canvas c{list, fonts_, textures_, backdrop_, time_};
    backdrop_.draw(list, textures_, dim_.value);

    const bool launching = !selected_game_.empty();
    const float zoom =
        launching ? 1.0f + 0.045f * tween::cubic_in_out(launch_.progress()) : 1.0f;
    list.push_transform(zoom, 960.0f, 540.0f, 0.0f, 0.0f);
    if (transition_.running)
    {
        // The old screen slides away as the new one arrives from the other side.
        const float t = transition_.progress();
        const float e = tween::cubic_in_out(t);
        const float direction = forward_ ? 1.0f : -1.0f;
        list.push_opacity(1.0f - tween::cubic_out(t / 0.55f));
        list.push_transform(1.0f, 0.0f, 0.0f, -direction * 56.0f * e, 0.0f);
        draw_screen(c, leaving_);
        list.pop_transform();
        list.pop_opacity();
        list.push_opacity(tween::cubic_out((t - 0.2f) / 0.8f));
        list.push_transform(1.0f, 0.0f, 0.0f, direction * 56.0f * (1.0f - e), 0.0f);
        draw_screen(c, screen_);
        list.pop_transform();
        list.pop_opacity();
    }
    else
    {
        draw_screen(c, screen_);
    }
    if (modal_shown_ != Modal::none)
    {
        const float opened = tween::clamp01(modal_open_.value);
        list.rounded_rect(kScreen, 0.0f, theme::kScrim.with_alpha(0.69f * opened));
        if (modal_shown_ == Modal::game)
            draw_game(c, opened);
        else
            draw_dialog(c, modal_shown_, opened);
    }
    list.pop_transform();
    if (launching)
        draw_launch(c);

    // The launcher arrives out of the dark.
    const float arrival = 1.0f - tween::cubic_out(intro_ / (first_start_ ? 0.7f : 0.45f));
    if (arrival > 0.0f)
        list.rounded_rect(kScreen, 0.0f, Color{0.0f, 0.0f, 0.0f, arrival});
}

} // namespace pe::ui
