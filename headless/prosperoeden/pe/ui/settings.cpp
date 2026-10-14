// ProsperoEden - Launcher settings: the category list and its dialogs.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace pe::ui
{

using audio::Cue;

namespace
{

enum Category
{
    kProfiles,
    kVideo,
    kPerformance,
    kAudio,
    kControls,
    kAccessibility,
    kDiagnostics,
    kFiles,
    kLanguage,
    kThemes,
    kDownloads,
    kSaveSync,
    kMultiplayer,
    kCategoryCount,
};
constexpr const char *kCategories[kCategoryCount] = {
    TR("Profiles"), TR("Video"), TR("Performance"), TR("Audio"), TR("Controls"), TR("Accessibility"),
    TR("System"), TR("Data Management"), TR("Language"), TR("Themes"), TR("Downloads"), TR("Save sync"), TR("Multiplayer")};

// The Video dialog's rows, and the window that shows five of them (placed as a game's settings
// are).
enum VideoRow : int
{
    video_renderer,
    video_output,
    video_resolution,
    video_filter,
    video_astc,
    video_refresh,
    video_overlay,
    video_overlay_options,
    video_frame_gen,
    video_frame_gen_target,
    video_frame_gen_multiplier,
    kVideoRows,
};
enum HudRow : int {
    hud_colors_row, hud_size_row, hud_corner_row, hud_backend_row, hud_fps_row,
    hud_ms_row, hud_speed_row, hud_cpu_row, hud_gpu_row, hud_mem_row, hud_ping_row,
    hud_cores_row, kHudRows
};
constexpr float kVideoRowsTop = 228.0f;
constexpr float kVideoRowPitch = 96.0f;
constexpr float kVideoRowHeight = 94.0f;
constexpr int kVideoRowsShown = 6;
constexpr Rect kVideoWindow{714.0f, kVideoRowsTop, 1086.0f,
                            kVideoRowPitch * (kVideoRowsShown - 1) + kVideoRowHeight};
// The Performance dialog's switches, and the window that shows four of them over what the
// highlighted one does.
enum PerformanceRow : int
{
    speed_block_list,
    speed_async_shaders,
    speed_fast_gpu,
    speed_unsafe_cpu,
    speed_unsafe_dma,
    speed_reactive_flushing,
    speed_skip_invalidation,
    kPerformanceRows,
};
constexpr int kPerformanceRowsShown = 5;
constexpr Rect kPerformanceWindow{714.0f, kVideoRowsTop, 1086.0f,
                                  kVideoRowPitch * (kPerformanceRowsShown - 1) + kVideoRowHeight};
// The sizes of Preferences::output, as every language writes them.
constexpr const char *kOutputs[] = {"1080p", "1440p", "2160p"};

const char *output_name(int output)
{
    return kOutputs[std::clamp(output, 0, 2)];
}

const char *on_off(bool value)
{
    return value ? tr("On") : tr("Off");
}

std::string percent(int value)
{
    return fill(tr("{0}%"), {std::to_string(value)});
}

std::string pick(const std::vector<std::string> &values, int index)
{
    return index >= 0 && index < static_cast<int>(values.size()) ?
               values[static_cast<std::size_t>(index)] : std::string{"-"};
}

} // namespace

Launcher::Modal Launcher::settings_modal() const
{
    switch (settings_.selected)
    {
    case kVideo: return Modal::video;
    case kPerformance: return Modal::performance;
    case kAudio: return Modal::audio;
    case kControls: return Modal::controls;
    case kAccessibility: return Modal::accessibility;
    case kDiagnostics: return Modal::diagnostics;
    default: return Modal::none;
    }
}

void Launcher::enter_settings_content()
{
    settings_content_ = true;
    option_ = 0;
    message_.clear();
    video_rows_.visible = kVideoRowsShown;
    video_rows_.pitch = kVideoRowPitch;
    video_rows_.wrap = false;
    video_rows_.reset(dialog_rows(Modal::video), 0);
    performance_rows_.visible = kPerformanceRowsShown;
    performance_rows_.pitch = kVideoRowPitch;
    performance_rows_.wrap = false;
    performance_rows_.reset(kPerformanceRows, 0);
    option_cursor_.snap(dialog_row_top(settings_modal(), 0));
    const auto states = switch_states(settings_modal());
    for (std::size_t i = 0; i < states.size(); ++i)
        switches_[i].snap(states[i] ? 1.0f : 0.0f);
    cue(Cue::focus);
}

void Launcher::press_settings(Key key)
{
    if (!settings_content_)
    {
        if (key == Key::circle)
        {
            open(Screen::home, false);
            return;
        }
        if (key == Key::up || key == Key::down)
        {
            if (settings_.move(key == Key::down ? 1 : -1))
            {
                section_.snap(0.0f);
                option_ = 0;
                message_.clear();
                cue(Cue::focus);
            }
            return;
        }
        if (key == Key::cross || key == Key::right)
            enter_settings_content();
        return;
    }
    if (key == Key::circle || key == Key::left)
    {
        settings_content_ = false;
        message_.clear();
        cue(Cue::back);
        return;
    }
    if (settings_.selected == kThemes)
    {
        if (key == Key::up || key == Key::down)
        {
            option_ = 1 - std::clamp(option_, 0, 1);
            cue(Cue::focus);
        }
        else if (key == Key::cross || key == Key::right)
        {
            const bool saved = set_ui_theme(option_ == 0);
            if (!saved)
                say(tr("Could not save. Please try again."), true);
            cue(saved ? Cue::toggle : Cue::error);
        }
        return;
    }
    const Modal editor = settings_modal();
    if (editor == Modal::none)
    {
        if (key != Key::cross && key != Key::right)
            return;
        switch (settings_.selected)
        {
        case kProfiles: open_profiles(); break;
        case kFiles: open(Screen::files, true); enter_files(); break;
        case kLanguage: open(Screen::language, true); enter_language(); break;
        case kDownloads: open_sources(); break;
        case kSaveSync: open_sync_setup(); break;
        case kMultiplayer: open_multiplayer(); break;
        default: break;
        }
        return;
    }
    if (key == Key::cross || key == Key::right)
    {
        if (editor == Modal::video && option_ == video_overlay_options) {
            hud_rows_.visible = kVideoRowsShown;
            hud_rows_.pitch = kVideoRowPitch;
            hud_rows_.wrap = false;
            hud_rows_.reset(kHudRows, 0);
            option_ = 0;
            open_modal(Modal::hud);
            return;
        }
        const bool choice = (editor == Modal::video && option_ != video_overlay) ||
            (editor == Modal::audio && option_ != 1) || (editor == Modal::controls && option_ == 1);
        if (choice)
        {
            if(editor == Modal::video && option_ >= video_frame_gen && services_.frame_gen_state() != 2) {
                say(tr("Frame generation needs Lossless.dll, from Lossless Scaling, in the lossless folder."),true); cue(Cue::error); return;
            }
            open_settings_choice();
            return;
        }
    }
    // Use the original preference writer and its rollback behavior. A pane is not a modal:
    // the temporary discriminator only selects the same existing editor action.
    modal_ = editor;
    press_dialog(key);
    if (modal_ == editor)
        modal_ = Modal::none;
    option_cursor_.snap(dialog_row_top(editor, option_));
}

void Launcher::draw_settings(Canvas &c)
{
    auto &list = c.list;
    draw_frame(c, tr("System Settings"), "");
    // The measured stock sidebar is a darker surface, not a custom vertical divider.
    list.rounded_rect({96.0f, 195.0f, 510.0f, 722.0f}, 0.0f, theme::kPanel);
    list.push_clip({150.0f,203.0f,450.0f,698.0f});
    for (int row = settings_.first_row(); row <= settings_.last_row(); ++row)
    {
        const Rect r{156.0f, 209.0f + 68.0f * row - settings_.scroll(), 438.0f, 66.0f};
        const bool selected = row == settings_.selected;
        if (selected)
        {
            if (!settings_content_)
                plate_focus(c, kRowPlate, r, 1.0f);
            list.rounded_rect({171.0f, r.y + 12.0f, 4.0f, r.h - 24.0f}, 0.0f, theme::kLime);
        }
        text_shrink(c, tr(kCategories[row]), 188.0f, baseline(r.y, r.h, 30.0f), 30.0f,
                    selected ? theme::kLime : theme::kText, 385.0f);
    }
    list.pop_clip();
    scrollbar(c,settings_,608.0f,209.0f,678.0f);
    const Modal editor = settings_modal();
    if (editor != Modal::none)
        draw_dialog(c, editor, 1.0f);
    else if (settings_.selected == kThemes)
    {
        const char *names[] = {TR("Basic White"), TR("Basic Black")};
        for (int row = 0; row < 2; ++row)
        {
            const Rect r{714.0f, 228.0f + 114.0f * row, 1086.0f, 112.0f};
            plate_rest(c, kRowPlate, r);
            if (settings_content_ && option_ == row)
                plate_focus(c, kRowPlate, r, 1.0f);
            list.bordered_rect({750.0f, r.y + 24.0f, 64.0f, 64.0f}, 0.0f,
                               Color::rgb(row == 0 ? 0xf4f4f4 : 0x303030), 1.0f, theme::kRule);
            text(c, tr(names[row]), 846.0f, baseline(r.y, r.h, 30.0f), 30.0f, theme::kText);
            if (presentation_.white_theme == (row == 0))
            {
                list.line(1730.0f, r.y + 53.0f, 1742.0f, r.y + 65.0f, 4.0f, theme::kLime);
                list.line(1742.0f, r.y + 65.0f, 1766.0f, r.y + 39.0f, 4.0f, theme::kLime);
            }
        }
    }
    else
    {
        const Rect r{714.0f, 228.0f, 1086.0f, 112.0f};
        plate_rest(c, kRowPlate, r);
        if (settings_content_)
            plate_focus(c, kRowPlate, r, 1.0f);
        std::string action;
        std::string copy;
        if (settings_.selected == kProfiles)
        {
            action = tr("User Settings");
            copy = tr("Each user keeps their own save data, settings and recent software.");
            for (std::size_t i = 0; i < profiles_.size(); ++i)
            {
                const float x = 750.0f + static_cast<float>(i) * 124.0f;
                list.circle(x + 44.0f, 439.0f, 44.0f, Color::rgb(0x438cc8));
                if (c.textures.stock_icons())
                    list.image(c.textures.stock_icons(), {x + 14.0f, 409.0f, 60.0f, 60.0f},
                        {256.0f/384.0f, 0, 128.0f/384.0f, 1}, theme::kText);
                if (profiles_[i].playing)
                    list.ring(x + 44.0f, 439.0f, 50.0f, 3.0f, theme::kLime);
            }
        }
        else if (settings_.selected == kFiles)
        {
            action = tr("Manage Game Files");
            copy = services_.files_folder();
        }
        else if (settings_.selected == kDownloads)
        {
            action = tr("Downloads");
            copy = tr("Games on your network, downloaded when you play them.");
            copy += "\n" + (sources_.configured ? fill(tr("{0} sources"),{std::to_string(sources_.list.size())}) : std::string{tr("Not set up")});
        }
        else if (settings_.selected == kSaveSync)
        {
            action = tr("Save sync");
            copy = tr("Each profile's save data on a server of its own, synced before and after a game.");
        }
        else if (settings_.selected == kMultiplayer)
        {
            action = tr("Multiplayer");
            copy = tr("Join an Eden room, then choose Local Wireless in a supported game. Everyone needs a compatible game version.");
            copy += "\n" + std::string(services_.room_status().phase == Eden::Multiplayer::Phase::Connected ? tr("Connected") : tr("Off"));
        }
        else
        {
            action = tr("Language");
            copy = pick(services_.language_labels(), prefs_.language);
        }
        text(c, action, 750.0f, baseline(r.y, r.h, 30.0f), 30.0f, theme::kText);
        text_block(c, copy, 750.0f, 550.0f, 24.0f, 36.0f, theme::kCopy, 1008.0f, 3, kShrink);
    }
    if (!message_.empty())
        notice_block(c, message_, 714.0f, 887.0f, 24.0f, 30.0f,
                     message_warning_ ? theme::kWarning : theme::kCopy, 1086.0f, 2, message_warning_);
    static constexpr Hint hints[] = {{Pad::circle, TR("Back")}, {Pad::cross, TR("OK")}};
    draw_footer(c, hints, 2);
}

namespace
{
struct SettingChoices
{
    std::string title;
    std::vector<std::string> values;
    int selected = 0;
};

SettingChoices setting_choices(int category, int option, const Preferences &prefs, Services &services)
{
    if (category == kVideo)
    {
        switch (option)
        {
        case video_renderer: return {tr("Renderer"), {"OpenGL", "Vulkan"}, prefs.renderer};
        case video_output: return {tr("Output resolution"), {"1080p", "1440p", "2160p"}, prefs.output};
        case video_resolution: return {tr("Resolution"), services.resolution_labels(), prefs.resolution};
        case video_filter: return {tr("Upscaling filter"), services.filter_labels(), prefs.filter};
        case video_astc: return {tr("ASTC textures"), services.astc_labels(), prefs.astc};
        case video_frame_gen: return {tr("Frame generation"), {tr("Off"),tr("On")},prefs.frame_gen ? 1 : 0};
        case video_frame_gen_target: return {tr("Frame gen target"),{tr("Auto"),"60 Hz","90 Hz","120 Hz","144 Hz","240 Hz"},prefs.frame_gen_target};
        case video_frame_gen_multiplier: return {tr("Frame gen multiplier"),{"2x","3x","4x"},prefs.frame_gen_multiplier};
        default: return {tr("Refresh rate"), {"60 Hz", "120 Hz"}, prefs.refresh};
        }
    }
    if (category == kControls)
        return {tr("Vibration strength"), {percent(25), percent(50), percent(75), percent(100)},
                std::clamp(prefs.vibration_strength / 25 - 1, 0, 3)};
    SettingChoices choices;
    choices.title = option == 0 ? tr("Game volume") : tr("Menu sounds");
    for (int volume = 0; volume <= 100; volume += 10)
        choices.values.push_back(percent(volume));
    choices.selected = (option == 0 ? prefs.volume : prefs.menu_volume) / 10;
    return choices;
}
} // namespace

void Launcher::open_settings_choice()
{
    setting_choice_category_ = settings_.selected;
    setting_choice_option_ = option_;
    const auto choices = setting_choices(setting_choice_category_, setting_choice_option_, prefs_, services_);
    setting_choice_rows_.visible = 6;
    setting_choice_rows_.pitch = 90.0f;
    setting_choice_rows_.wrap = false;
    setting_choice_rows_.reset(static_cast<int>(choices.values.size()), choices.selected);
    open_modal(Modal::settings_choice);
}

void Launcher::press_settings_choice(Key key)
{
    if (key == Key::circle)
    {
        close_modal();
        option_ = setting_choice_option_;
        return;
    }
    if (key == Key::up || key == Key::down)
    {
        if (setting_choice_rows_.move(key == Key::down ? 1 : -1))
            cue(Cue::focus);
        return;
    }
    if (key != Key::cross)
        return;
    const Preferences before = prefs_;
    const int value = setting_choice_rows_.selected;
    if (setting_choice_category_ == kVideo)
    {
        switch (setting_choice_option_)
        {
        case video_renderer: prefs_.renderer = value; break;
        case video_output: prefs_.output = value; break;
        case video_resolution: prefs_.resolution = value; break;
        case video_filter: prefs_.filter = value; break;
        case video_astc: prefs_.astc = value; break;
        case video_frame_gen: prefs_.frame_gen = value != 0; break;
        case video_frame_gen_target: prefs_.frame_gen_target = value; break;
        case video_frame_gen_multiplier: prefs_.frame_gen_multiplier = value; break;
        default: prefs_.refresh = value; break;
        }
    }
    else if (setting_choice_category_ == kControls)
        prefs_.vibration_strength = (value + 1) * 25;
    else if (setting_choice_option_ == 0)
        prefs_.volume = value * 10;
    else
        prefs_.menu_volume = value * 10;
    if (!save_preferences(true))
    {
        prefs_ = before;
        cue(Cue::error);
        return;
    }
    if (setting_choice_category_ == kControls && prefs_.vibration)
        services_.vibration_pulse(prefs_.vibration_strength);
    apply_look();
    close_modal();
    option_ = setting_choice_option_;
    if (setting_choice_category_ == kVideo && setting_choice_option_ == video_refresh && prefs_.refresh == 1)
        say(tr("Saved. A display that cannot show 120 Hz stays at 60 Hz."));
    else if (setting_choice_category_ == kVideo && setting_choice_option_ == video_astc && prefs_.astc == 1)
        say(tr("Saved. Full quality uses four times the graphics memory for these textures and can run out above 1x."));
    cue(Cue::saved);
}

void Launcher::draw_settings_choice(Canvas &c, float open)
{
    const auto choices = setting_choices(setting_choice_category_, setting_choice_option_, prefs_, services_);
    c.list.push_opacity(open);
    glass(c, {390.0f, 135.0f, 1140.0f, 810.0f}, 0.0f, theme::kPanel, theme::kPanelEdge, 1.0f);
    text(c, choices.title, 450.0f, 212.0f, 36.0f, theme::kTitle);
    c.list.push_clip({420.0f, 255.0f, 1080.0f, 550.0f});
    for (int row = setting_choice_rows_.first_row(); row <= setting_choice_rows_.last_row(); ++row)
    {
        const Rect r{450.0f, 264.0f + row * 90.0f - setting_choice_rows_.scroll(), 1020.0f, 88.0f};
        plate(c, kRowPlate, r, row == setting_choice_rows_.selected ? 1.0f : 0.0f);
        text_shrink(c, choices.values[static_cast<std::size_t>(row)], 486.0f,
                    baseline(r.y, r.h, 30.0f), 30.0f, theme::kText, 900.0f);
        if (row == choices.selected)
        {
            c.list.line(1386.0f, r.y + 42.0f, 1398.0f, r.y + 54.0f, 4.0f, theme::kLime);
            c.list.line(1398.0f, r.y + 54.0f, 1422.0f, r.y + 28.0f, 4.0f, theme::kLime);
        }
    }
    c.list.pop_clip();
    scrollbar(c, setting_choice_rows_, 1484.0f, 264.0f, 538.0f);
    if (!message_.empty())
        notice(c, message_, 450.0f, 839.0f, 24.0f, theme::kWarning, 1020.0f, true);
    static constexpr Hint hints[] = {{Pad::circle, TR("Back")}, {Pad::cross, TR("OK")}};
    draw_hints_right(c,hints,2,1470.0f,897.0f,theme::kText);
    c.list.pop_opacity();
}

// ---------------------------------------------------------------- dialogs

int Launcher::dialog_rows(Modal modal) const
{
    switch (modal)
    {
    case Modal::video:
        return services_.frame_gen_state() == 0 ? static_cast<int>(video_frame_gen) : static_cast<int>(kVideoRows);
    case Modal::hud: return kHudRows;
    case Modal::performance:
        return kPerformanceRows;
    case Modal::audio:
    case Modal::accessibility:
        return 3;
    case Modal::game:
        // Console mode, video, performance, audio, controls, system, language, mods; save data in builds
        // that move saves.
        return (services_.save_transfer_available() ? 9 : 8) +
               (library_.selected < static_cast<int>(games_.size()) &&
                !games_[static_cast<std::size_t>(library_.selected)].sources.empty() &&
                !games_[static_cast<std::size_t>(library_.selected)].remote ? 1 : 0);
    case Modal::controls: return 3;
    case Modal::diagnostics: return 2;
    default:
        return 1;
    }
}

float Launcher::dialog_row_top(Modal modal, int row) const
{
    switch (modal)
    {
    case Modal::audio:
    case Modal::accessibility:
        return kVideoRowsTop + kVideoRowPitch * static_cast<float>(row);
    case Modal::performance: // four of its rows show; the list scrolls to the others
        return kVideoRowsTop + kVideoRowPitch * static_cast<float>(row) - performance_rows_.scroll();
    case Modal::video: // five of its rows show; the list scrolls to the others
        return kVideoRowsTop + kVideoRowPitch * static_cast<float>(row) - video_rows_.scroll();
    case Modal::hud:
        return kVideoRowsTop + kVideoRowPitch * static_cast<float>(row) - hud_rows_.scroll();
    case Modal::game:
        return 334.0f + 96.0f * static_cast<float>(row);
    case Modal::controls: // under the shortcuts
        return 528.0f + 96.0f * static_cast<float>(row);
    case Modal::diagnostics: return 648.0f + 96.0f * row;
    default:
        return 744.0f;
    }
}

void Launcher::press_dialog(Key key)
{
    const int rows = dialog_rows(modal_);
    const bool adjust = key == Key::left || key == Key::right;
    const bool activate = key == Key::cross;
    const int step = key == Key::left ? -1 : 1;
    if (key == Key::circle)
    {
        if (modal_ == Modal::hud) {
            modal_ = modal_shown_ = Modal::none;
            option_ = video_overlay_options;
            message_.clear();
            cue(Cue::back);
            return;
        }
        close_modal();
        return;
    }
    if ((key == Key::up || key == Key::down) && (modal_ == Modal::video || modal_ == Modal::performance || modal_ == Modal::hud))
    {
        ListView &rows_view = modal_ == Modal::video ? video_rows_ : modal_ == Modal::hud ? hud_rows_ : performance_rows_;
        if (rows_view.move(key == Key::down ? 1 : -1))
        {
            option_ = rows_view.selected;
            message_.clear();
            cue(Cue::focus);
        }
        return;
    }
    if ((key == Key::up || key == Key::down) && rows > 1)
    {
        option_ = (option_ + (key == Key::down ? 1 : rows - 1)) % rows;
        message_.clear();
        cue(Cue::focus);
        return;
    }
    if (!adjust && !activate)
        return;

    const Preferences before = prefs_;
    Cue sound = Cue::toggle;
    switch (modal_)
    {
    case Modal::video:
        if (option_ == video_renderer)
            prefs_.renderer = prefs_.renderer != 0 ? 0 : 1;
        else if (option_ == video_output)
        {
            // The menu follows at once (the frontend opens its display again at this size).
            const int count = static_cast<int>(std::size(kOutputs));
            prefs_.output = (std::clamp(prefs_.output, 0, count - 1) + step + count) % count;
        }
        else if (option_ == video_resolution)
        {
            const int count = static_cast<int>(services_.resolution_labels().size());
            prefs_.resolution = (prefs_.resolution + step + count) % count;
        }
        else if (option_ == video_filter)
        {
            const int count = static_cast<int>(services_.filter_labels().size());
            prefs_.filter = (prefs_.filter + step + count) % count;
        }
        else if (option_ == video_astc)
            prefs_.astc = prefs_.astc != 0 ? 0 : 1;
        else if (option_ == video_refresh)
            prefs_.refresh = prefs_.refresh != 0 ? 0 : 1;
        else if (option_ == video_overlay_options) return;
        else if(option_ >= video_frame_gen) {
            if(services_.frame_gen_state() != 2) { say(tr("Frame generation needs Lossless.dll, from Lossless Scaling, in the lossless folder."),true); cue(Cue::error); return; }
            if(option_ == video_frame_gen) prefs_.frame_gen = !prefs_.frame_gen;
            else if(option_ == video_frame_gen_target) prefs_.frame_gen_target = (std::clamp(prefs_.frame_gen_target,0,5)+step+6)%6;
            else prefs_.frame_gen_multiplier = (std::clamp(prefs_.frame_gen_multiplier,0,2)+step+3)%3;
        }
        else
            prefs_.hud = !prefs_.hud;
        break;
    case Modal::hud:
        if (option_ == hud_colors_row) prefs_.hud_colors = !prefs_.hud_colors;
        else if (option_ == hud_size_row) prefs_.hud_size = (std::clamp(prefs_.hud_size, 0, 2) + step + 3) % 3;
        else if (option_ == hud_corner_row) prefs_.hud_corner = (std::clamp(prefs_.hud_corner, 0, 3) + step + 4) % 4;
        else prefs_.hud_fields ^= 1 << (option_ - hud_backend_row);
        sound = option_ == hud_size_row || option_ == hud_corner_row ? Cue::slider :
            (option_ == hud_colors_row ? prefs_.hud_colors :
             (((prefs_.hud_fields & (1 << (option_ - hud_backend_row))) != 0) != (option_ == hud_cores_row))) ?
                Cue::toggle : Cue::toggle_off;
        break;
    case Modal::audio:
        if (option_ == 0)
        {
            if (!adjust)
                return;
            prefs_.volume = std::clamp(prefs_.volume + 10 * step, 0, 100);
            sound = Cue::slider;
        }
        else if (option_ == 1)
            prefs_.mute = !prefs_.mute;
        else
        {
            if (!adjust)
                return;
            prefs_.menu_volume = std::clamp(prefs_.menu_volume + 10 * step, 0, 100);
            sound = Cue::slider;
        }
        break;
    case Modal::performance:
    {
        bool *const switches[kPerformanceRows] = {
            &prefs_.block_list, &prefs_.async_shaders,     &prefs_.fast_gpu, &prefs_.unsafe_cpu,
            &prefs_.unsafe_dma, &prefs_.reactive_flushing, &prefs_.skip_invalidation};
        bool &value = *switches[std::clamp(option_, 0, kPerformanceRows - 1)];
        value = !value;
        break;
    }
    case Modal::controls:
        if (option_ == 1) {
            if (!adjust) return;
            prefs_.vibration_strength = std::clamp(prefs_.vibration_strength + 25 * step, 25, 100);
            sound = Cue::slider;
            break;
        }
        if (option_ == 2)
        {
            // The button mapping has its own list (game_options.cpp).
            if (activate)
                open_mapping(false);
            return;
        }
        prefs_.vibration = !prefs_.vibration;
        break;
    case Modal::accessibility:
        if (option_ == 0)
            prefs_.large_text = !prefs_.large_text;
        else if (option_ == 1)
            prefs_.high_contrast = !prefs_.high_contrast;
        else
            prefs_.reduce_motion = !prefs_.reduce_motion;
        break;
    case Modal::diagnostics:
        if(option_ == 0) prefs_.detailed_logging = !prefs_.detailed_logging;
        else prefs_.immediate_logs = !prefs_.immediate_logs;
        break;
    default:
        return;
    }
    // The launcher's look changes on the spot: that is its own confirmation.
    if (!save_preferences(modal_ == Modal::accessibility))
    {
        prefs_ = before;
        sound = Cue::error;
    }
    else if (modal_ == Modal::video && option_ == video_refresh && prefs_.refresh == 1)
    {
        // 120 Hz is a request: the display has the last word.
        say(tr("Saved. A display that cannot show 120 Hz stays at 60 Hz."));
    }
    else if (modal_ == Modal::video && option_ == video_astc && prefs_.astc == 1)
    {
        say(tr("Saved. Full quality uses four times the graphics memory for these textures and can run out above 1x."));
    }
    if (modal_ == Modal::controls && option_ == 1 && sound != Cue::error && prefs_.vibration)
        services_.vibration_pulse(prefs_.vibration_strength);
    apply_look();
    if (sound == Cue::toggle && modal_ != Modal::hud)
    {
        const auto states = switch_states(modal_);
        const int state = modal_ == Modal::video || modal_ == Modal::controls || modal_ == Modal::diagnostics ? 0 :
                          modal_ == Modal::audio ? 0 : option_;
        sound = states[static_cast<std::size_t>(std::clamp(state,0,6))] ? Cue::toggle : Cue::toggle_off;
    }
    cue(sound);
}

void Launcher::draw_dialog(Canvas &c, Modal modal, float open)
{
    auto &list = c.list;
    list.push_opacity(open);
    const int rows = dialog_rows(modal);
    const bool scrolls = modal == Modal::video || modal == Modal::performance || modal == Modal::hud;
    const ListView &view = modal == Modal::performance ? performance_rows_ : modal == Modal::hud ? hud_rows_ : video_rows_;
    const Rect &window = modal == Modal::performance ? kPerformanceWindow : kVideoWindow;
    const int first = scrolls ? view.first_row() : 0;
    const int last = scrolls ? view.last_row() : rows - 1;
    const bool focus = settings_content_ && (modal_ == Modal::none || modal_ == Modal::hud);
    if (scrolls)
        list.push_clip({708.0f, window.y - 6.0f, 1110.0f, window.h + 12.0f});
    for (int row = first; row <= last; ++row)
    {
        const Rect r{714.0f, dialog_row_top(modal, row), 1086.0f, 94.0f};
        plate_rest(c, kRowPlate, r);
        if (focus && row == option_)
            plate_focus(c, kRowPlate, r, 1.0f);
    }
    constexpr float kValueRight = 1764.0f;
    const auto value = [&](int row, const std::string &name, const std::string &shown, bool on = false)
    {
        const float y = dialog_row_top(modal, row);
        const float used = text_shrink(c, shown, kValueRight, baseline(y, 94.0f, 30.0f),
                                      30.0f, on ? theme::kLime : theme::kCopy, 624.0f, Align::right);
        text_shrink(c, name, 750.0f, baseline(y, 94.0f, 30.0f), 30.0f,
                    theme::kText, 1014.0f - used - 30.0f);
    };
    const auto switch_row = [&](int row, const char *name, bool on)
    { value(row, name, on_off(on), on); };
    switch (modal)
    {
    case Modal::video:
    {
        const std::string values[] = {
            prefs_.renderer != 0 ? "Vulkan" : "OpenGL", output_name(prefs_.output),
            pick(services_.resolution_labels(), prefs_.resolution),
            pick(services_.filter_labels(), prefs_.filter), pick(services_.astc_labels(), prefs_.astc), hertz(prefs_.refresh), std::string{}, ">", on_off(prefs_.frame_gen),
            prefs_.frame_gen_target <= 0 ? std::string{tr("Auto")} : fill(tr("{0} Hz"),{std::vector<std::string>{"Auto","60","90","120","144","240"}[static_cast<std::size_t>(std::clamp(prefs_.frame_gen_target,1,5))]}),
            std::vector<std::string>{"2x","3x","4x"}[static_cast<std::size_t>(std::clamp(prefs_.frame_gen_multiplier,0,2))]};
        static constexpr const char *names[] = {TR("Renderer"), TR("Output resolution"),
            TR("Resolution"), TR("Upscaling filter"), TR("ASTC textures"), TR("Refresh rate"), TR("FPS overlay"), TR("Overlay options"), TR("Frame generation"), TR("Frame gen target"), TR("Frame gen multiplier")};
        for (int row = first; row <= last; ++row)
            if (row == video_overlay)
                switch_row(row, tr(names[row]), prefs_.hud);
            else if(row >= video_frame_gen && services_.frame_gen_state() != 2) {
                list.push_opacity(.38f); value(row,tr(names[row]),tr("Unavailable")); list.pop_opacity();
            } else value(row, tr(names[row]), values[row]);
        break;
    }
    case Modal::hud:
    {
        static constexpr const char *names[kHudRows] = {TR("Enable colors"), TR("Size"), TR("Corner"),
            TR("Backend"), TR("FPS"), TR("Frame time"), TR("Speed"), TR("CPU"), TR("GPU"),
            TR("Memory"), TR("Ping"), TR("CPU by core")};
        for (int row = first; row <= last; ++row) {
            if (row == hud_colors_row) switch_row(row, tr(names[row]), prefs_.hud_colors);
            else if (row == hud_size_row) value(row, tr(names[row]), tr(kHudSizeNames[std::clamp(prefs_.hud_size, 0, 2)]));
            else if (row == hud_corner_row) value(row, tr(names[row]), tr(kHudCornerNames[std::clamp(prefs_.hud_corner, 0, 3)]));
            else switch_row(row, tr(names[row]), ((prefs_.hud_fields & (1 << (row - hud_backend_row))) != 0) != (row == hud_cores_row));
        }
        break;
    }
    case Modal::performance:
    {
        static constexpr const char *names[] = {TR("Compile ahead"), TR("Asynchronous shaders"),
            TR("Faster GPU emulation"), TR("Faster CPU emulation"), TR("Faster DMA"),
            TR("Reactive flushing"), TR("Skip CPU invalidation")};
        const bool states[] = {prefs_.block_list, prefs_.async_shaders, prefs_.fast_gpu,
            prefs_.unsafe_cpu, prefs_.unsafe_dma, prefs_.reactive_flushing, prefs_.skip_invalidation};
        for (int row = first; row <= last; ++row)
            switch_row(row, tr(names[row]), states[row]);
        break;
    }
    case Modal::audio:
        value(0, tr("Game volume"), percent(prefs_.volume));
        switch_row(1, tr("Mute"), prefs_.mute);
        value(2, tr("Menu sounds"), prefs_.menu_volume ? percent(prefs_.menu_volume) : tr("Off"));
        break;
    case Modal::controls:
    {
        text(c, tr("Controllers"), 750.0f, 256.0f, 30.0f, theme::kText);
        // Connection is read from the PS5 controller service; no pairing or battery is invented.
        const unsigned connected = controllers_;
        for (int player = 0; player < 4; ++player)
        {
            const float x = 778.0f + player * 252.0f;
            const bool present = (connected & (1u << player)) != 0;
            if (present)
            {
                controller_icon(c, {x, 309.0f, 160.0f, 111.0f}, 1.0f);
                for (int light = 0; light < 4; ++light)
                    list.rounded_rect({x + 41.0f + light * 21.0f, 449.0f, 12.0f, 12.0f}, 0.0f,
                                      light <= player ? theme::kLime : theme::kRule);
            }
            else
            {
                text(c, "-", x + 80.0f, 392.0f, 32.0f, theme::kMeta, Align::center);
            }
        }
        text_shrink(c, tr("Connected controllers"), 750.0f, 511.0f, 24.0f, theme::kCopy, 1008.0f);
        switch_row(0, tr("Vibration"), prefs_.vibration);
        value(1, tr("Vibration strength"), percent(prefs_.vibration_strength));
        value(2, tr("Change Button Mapping"), prefs_.mapping == kDefaultMapping ? tr("Default") : tr("Changed"));
        list.line(714.0f, 826.0f, 1800.0f, 826.0f, 1.0f, theme::kRule);
        text_shrink(c, std::string(tr("Touchpad")) + " + L1  " + tr("End the game and return to this menu"),
                    750.0f, 858.0f, 24.0f, theme::kCopy, 1008.0f);
        text_shrink(c, std::string(tr("Touchpad")) + " + R1  " + tr("Show or hide the FPS overlay"),
                    750.0f, 902.0f, 24.0f, theme::kCopy, 1008.0f);
        break;
    }
    case Modal::accessibility:
        switch_row(0, tr("Larger text"), prefs_.large_text);
        switch_row(1, tr("High contrast"), prefs_.high_contrast);
        switch_row(2, tr("Reduce motion"), prefs_.reduce_motion);
        break;
    default:
        text(c, tr("System Information"), 750.0f, 258.0f, 30.0f, theme::kText);
        text_block(c, services_.setup_details(), 750.0f, 330.0f, 26.0f, 39.0f,
                   theme::kCopy, 1008.0f, 6, kShrink);
        text_shrink(c, services_.files_folder(), 750.0f, 612.0f, 24.0f, theme::kCopy, 1008.0f);
        switch_row(0, tr("Detailed logging"), prefs_.detailed_logging);
        switch_row(1, tr("Write logs at once"), prefs_.immediate_logs);
        if(option_ == 1) text_block(c,tr("Keeps the last line before a crash, but games stutter. From the next start."),750,878,22,28,theme::kMeta,1008,2,kShrink);
        break;
    }
    if (scrolls)
    {
        list.pop_clip();
        scrollbar(c, view, 1821.0f, window.y, window.h);
    }
    if (modal == Modal::performance)
    {
        static constexpr const char *about[] = {
            TR("Compiles code used in earlier sessions as a game starts."),
            TR("Vulkan draws effects once their shaders are ready, reducing compilation pauses."),
            TR("Lower GPU accuracy can be faster, but graphics may be incorrect."),
            TR("Less exact floating-point math may affect some games."),
            TR("Less exact GPU memory transfers may affect some graphics."),
            TR("Keeps GPU readback accurate. Disabling it may affect effects."),
            TR("Fewer memory invalidation checks may leave stale textures.")};
        text_block(c, tr(about[std::clamp(option_, 0, kPerformanceRows - 1)]), 750.0f,
                   772.0f, 24.0f, 33.0f, theme::kCopy, 1008.0f, 3, kShrink);
    }
    else if (modal == Modal::audio)
        text_block(c, tr("Game audio settings apply when software starts. Menu sound volume changes immediately."),
                   750.0f, 620.0f, 24.0f, 36.0f, theme::kCopy, 1008.0f, 3, kShrink);
    else if (modal == Modal::accessibility)
    {
        static constexpr const char *about[] = {TR("Draws the menu's small text larger."),
            TR("Solid surfaces and brighter text make the selection easier to see."),
            TR("Limits menu transitions and loading-screen motion.")};
        text_block(c, tr(about[std::clamp(option_, 0, 2)]), 750.0f, 620.0f, 24.0f,
                   36.0f, theme::kCopy, 1008.0f, 3, kShrink);
    }
    list.pop_opacity();
}

} // namespace pe::ui
