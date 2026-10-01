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

constexpr Rect kListPanel{108.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kDetailPanel{980.0f, 188.0f, 820.0f, 720.0f};
constexpr Rect kDialog{550.0f, 180.0f, 820.0f, 720.0f};
constexpr float kRowsTop = 264.0f;
constexpr float kRowHeight = 80.0f;
enum Category
{
    kVideo,
    kAudio,
    kControls,
    kAccessibility,
    kDiagnostics,
    kFiles,
    kLanguage,
    kCategoryCount,
};
constexpr const char *kCategories[kCategoryCount] = {
    TR("Video"), TR("Audio"), TR("Controls"), TR("Accessibility"), TR("Diagnostics"), TR("Game files"),
    TR("Language")};
// The same as headings: capitals differ by language, so each is its own text.
constexpr const char *kHeadings[kCategoryCount] = {
    TR("VIDEO"), TR("AUDIO"), TR("CONTROLS"), TR("ACCESSIBILITY"), TR("DIAGNOSTICS"), TR("GAME FILES"),
    TR("LANGUAGE")};

// The Video dialog's rows, and the window that shows five of them (placed as a game's settings
// are).
enum VideoRow : int
{
    video_renderer,
    video_output,
    video_resolution,
    video_filter,
    video_refresh,
    video_overlay,
    kVideoRows,
};
constexpr float kVideoRowsTop = 334.0f;
constexpr float kVideoRowPitch = 96.0f;
constexpr float kVideoRowHeight = 94.0f;
constexpr int kVideoRowsShown = 5;
constexpr Rect kVideoWindow{592.0f, kVideoRowsTop, 736.0f,
                            kVideoRowPitch * (kVideoRowsShown - 1) + kVideoRowHeight};
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

// A path that fits a label: the end is what tells folders apart.
std::string short_path(const std::string &path, std::size_t limit)
{
    return path.size() <= limit ? path : "..." + path.substr(path.size() - (limit - 3));
}

} // namespace

void Launcher::press_settings(Key key)
{
    switch (key)
    {
    case Key::circle:
        open(Screen::home, false);
        return;
    case Key::up:
    case Key::down:
        if (settings_.move(key == Key::down ? 1 : -1))
        {
            section_.value = 0.0f;
            section_.velocity = 0.0f;
            cue(Cue::focus);
        }
        return;
    case Key::cross:
        press_ = 1.0f;
        switch (settings_.selected)
        {
        case kFiles:
            open(Screen::files, true);
            enter_files();
            break;
        case kLanguage:
            open(Screen::language, true);
            enter_language();
            break;
        case kVideo:
            open_modal(Modal::video);
            video_rows_.visible = kVideoRowsShown;
            video_rows_.pitch = kVideoRowPitch;
            video_rows_.reset(kVideoRows, 0);
            break;
        case kAudio:
            open_modal(Modal::audio);
            break;
        case kControls:
            open_modal(Modal::controls);
            break;
        case kAccessibility:
            open_modal(Modal::accessibility);
            break;
        default:
            open_modal(Modal::diagnostics);
            break;
        }
        return;
    default:
        return;
    }
}

void Launcher::draw_settings(Canvas &c)
{
    gfx::DrawList &list = c.list;
    draw_frame(c, tr("Settings"), tr("Fine-tune your experience"));

    // ---- categories ----
    glass(c, kListPanel, 26.0f, theme::kPanel.with_alpha(0.80f), theme::kPanelEdge.with_alpha(0.55f));
    text(c, tr("PREFERENCES"), 138.0f, baseline(208.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    const auto row_rect = [&](int row) -> Rect
    { return {150.0f, kRowsTop + settings_.pitch * static_cast<float>(row), 736.0f, kRowHeight}; };
    for (int row = 0; row < kCategoryCount; ++row)
        plate_rest(c, kRowPlate, row_rect(row));
    plate_focus(c, kRowPlate, {150.0f, kRowsTop + settings_.cursor(), 736.0f, kRowHeight}, 1.0f);
    const std::string summaries[kCategoryCount] = {
        prefs_.renderer != 0 ? "Vulkan" : "OpenGL",
        prefs_.mute ? tr("Muted") : percent(prefs_.volume),
        prefs_.vibration ? tr("Vibration on") : tr("Vibration off"),
        prefs_.large_text || prefs_.high_contrast || prefs_.reduce_motion ? tr("On") : "",
        prefs_.detailed_logging ? tr("Detailed logs on") : "",
        "",
        pick(services_.language_labels(), prefs_.language),
    };
    for (int row = 0; row < kCategoryCount; ++row)
    {
        const Rect r = row_rect(row);
        // What the category is set to, then a chevron: there is more behind the row.
        const float summary =
            text_shrink(c, summaries[row], r.x + r.w - 62.0f, baseline(r.y, r.h, theme::kSmall),
                        theme::kSmall, theme::kMeta, 330.0f, Align::right);
        text_shrink(c, tr(kCategories[row]), r.x + 36.0f, baseline(r.y, r.h, theme::kText24),
                    theme::kText24, theme::kValue, r.w - 36.0f - 62.0f - summary - 24.0f);
        const float cx = r.x + r.w - 34.0f;
        const float cy = r.y + r.h * 0.5f;
        const Color ink = theme::kLimePale.with_alpha(row == settings_.selected ? 0.95f : 0.4f);
        list.line(cx - 4.0f, cy - 8.0f, cx + 4.0f, cy, 2.2f, ink);
        list.line(cx + 4.0f, cy, cx - 4.0f, cy + 8.0f, 2.2f, ink);
    }

    // ---- what the focused category holds ----
    glass(c, kDetailPanel, 26.0f, theme::kPanel.with_alpha(0.80f),
          theme::kPanelEdge.with_alpha(0.55f));
    text(c, tr("ON THIS CONSOLE"), 1016.0f, baseline(210.0f, 28.0f, theme::kSmall), theme::kSmall,
         theme::kLimePale, Align::left, 3.0f);
    text_shrink(c, tr("Make it yours."), 1016.0f, baseline(258.0f, 54.0f, theme::kLead),
                theme::kLead, theme::kTitle, 748.0f);
    text_block(c, tr("Adjust the essentials without leaving your library behind."), 1016.0f,
               baseline(332.0f, 36.0f, theme::kText24), theme::kText24, 36.0f, theme::kCopy, 748.0f,
               2, kShrink);
    list.rounded_rect({1016.0f, 432.0f, 748.0f, 1.0f}, 0.0f, theme::kRule);

    struct Line
    {
        const char *label;
        std::string value;
    };
    std::vector<Line> lines;
    const char *about = "";
    const std::string folder = services_.files_folder();
    const std::string saved_folder = services_.saved_files_folder();
    switch (settings_.selected)
    {
    case kVideo:
        about = tr("Graphics backend and how games are scaled to your TV.");
        lines = {{tr("RENDERER"), prefs_.renderer != 0 ? tr("Vulkan (recommended)") : "OpenGL"},
                 {tr("OUTPUT RESOLUTION"), output_name(prefs_.output)},
                 {tr("RESOLUTION"), pick(services_.resolution_labels(), prefs_.resolution)},
                 {tr("UPSCALING FILTER"), pick(services_.filter_labels(), prefs_.filter)},
                 {tr("REFRESH RATE"), hertz(prefs_.refresh)},
                 {tr("FPS OVERLAY"), on_off(prefs_.hud)}};
        break;
    case kAudio:
        about = tr("Game volume, and the sounds of this menu.");
        lines = {{tr("GAME VOLUME"), percent(prefs_.volume)},
                 {tr("MUTE"), on_off(prefs_.mute)},
                 {tr("MENU SOUNDS"), prefs_.menu_volume > 0 ? percent(prefs_.menu_volume) : tr("Off")}};
        break;
    case kControls:
        about = tr("Shortcuts during a game, and vibration.");
        lines = {{tr("VIBRATION"), on_off(prefs_.vibration)},
                 {tr("END GAME"), "Select + L1"},
                 {tr("FPS OVERLAY"), "Select + R1"}};
        break;
    case kAccessibility:
        about = tr("Make the menu easier to see and follow.");
        lines = {{tr("LARGER TEXT"), on_off(prefs_.large_text)},
                 {tr("HIGH CONTRAST"), on_off(prefs_.high_contrast)},
                 {tr("REDUCE MOTION"), on_off(prefs_.reduce_motion)}};
        break;
    case kDiagnostics:
        about = tr("Setup status and detailed logs.");
        lines = {{tr("SETUP"), home_.setup_ready ? tr("Ready") : tr("Needs attention")},
                 {tr("DETAILED LOGS"), on_off(prefs_.detailed_logging)}};
        break;
    case kFiles:
        about = tr("The folder that holds your keys, firmware and games.");
        lines = {{tr("IN USE"), short_path(folder, 34)}};
        if (!saved_folder.empty() && saved_folder != folder)
            lines.push_back({tr("NEXT START"), short_path(saved_folder, 34)});
        break;
    default:
        about = tr("The language games use when they offer it.");
        lines = {{tr("LANGUAGE"), pick(services_.language_labels(), prefs_.language)},
                 {tr("REGION"), services_.language_region(prefs_.language)}};
        break;
    }
    const float shown = tween::clamp01(section_.value);
    list.push_opacity(shown);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - shown) * 10.0f * motion());
    text(c, tr(kHeadings[settings_.selected]), 1016.0f, baseline(458.0f, 30.0f, theme::kSmall),
         theme::kSmall, theme::kLime, Align::left, 3.0f);
    text_shrink(c, about, 1016.0f, baseline(494.0f, 32.0f, 22.0f), 22.0f, theme::kCopy, 748.0f);
    // Five lines sit 62 apart; Video's six move closer to stay inside the panel.
    const float pitch = lines.size() > 5 ? 52.0f : 62.0f;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const float top = 562.0f + pitch * static_cast<float>(i);
        const float value =
            text_shrink(c, lines[i].value, 1764.0f, baseline(top, 36.0f, theme::kText24),
                        theme::kText24, theme::kValue, 470.0f, Align::right);
        text_shrink(c, lines[i].label, 1016.0f, baseline(top, 36.0f, theme::kSmall), theme::kSmall,
                    theme::kLabel, 748.0f - value - 24.0f, Align::left, 2.0f);
        list.rounded_rect({1016.0f, top + 48.0f, 748.0f, 1.0f}, 0.0f, theme::kRule.with_alpha(0.45f));
    }
    list.pop_transform();
    list.pop_opacity();

    static constexpr Hint kHints[] = {
        {Pad::cross, TR("Select")}, {Pad::circle, TR("Back")}, {Pad::updown, TR("Browse settings")}};
    draw_footer(c, kHints, 3);
}

// ---------------------------------------------------------------- dialogs

int Launcher::dialog_rows(Modal modal) const
{
    switch (modal)
    {
    case Modal::video:
        return kVideoRows;
    case Modal::audio:
    case Modal::accessibility:
        return 3;
    case Modal::game:
        // Console mode, renderer, resolution, filter, refresh rate, mods; save data in builds that
        // move saves.
        return services_.save_transfer_available() ? 7 : 6;
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
        return 370.0f + 102.0f * static_cast<float>(row);
    case Modal::video: // five of its rows show; the list scrolls to the others
        return kVideoRowsTop + kVideoRowPitch * static_cast<float>(row) - video_rows_.scroll();
    case Modal::game:
        return 334.0f + 96.0f * static_cast<float>(row);
    default:
        return 670.0f;
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
        close_modal();
        return;
    }
    if ((key == Key::up || key == Key::down) && modal_ == Modal::video)
    {
        if (video_rows_.move(key == Key::down ? 1 : -1))
        {
            option_ = video_rows_.selected;
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
        else if (option_ == video_refresh)
            prefs_.refresh = prefs_.refresh != 0 ? 0 : 1;
        else
            prefs_.hud = !prefs_.hud;
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
    case Modal::controls:
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
        prefs_.detailed_logging = !prefs_.detailed_logging;
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
    apply_look();
    cue(sound);
}

void Launcher::draw_dialog(Canvas &c, Modal modal, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.03f * (1.0f - open) * motion(), 960.0f, 540.0f, 0.0f,
                        (1.0f - open) * 26.0f * motion());
    glass(c, kDialog, 26.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);

    const char *title = "";
    const char *copy = "";
    switch (modal)
    {
    case Modal::video:
        title = tr("Video");
        copy = tr("How games are drawn and scaled to your TV.");
        break;
    case Modal::audio:
        title = tr("Audio");
        copy = tr("Game audio; PS5 system-menu music is unchanged.");
        break;
    case Modal::controls:
        title = tr("Controls");
        copy = tr("Controller shortcuts and supported features.");
        break;
    case Modal::accessibility:
        title = tr("Accessibility");
        copy = tr("Make the menu easier to see and follow.");
        break;
    default:
        title = tr("Diagnostics");
        copy = tr("Detailed logs apply to the next game launch.");
        break;
    }
    text_shrink(c, title, 592.0f, baseline(218.0f, 62.0f, theme::kDisplay), theme::kDisplay,
                theme::kTitle, 736.0f);
    text_shrink(c, copy, 592.0f, baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall,
                Color::rgb(0xbecbb9), 736.0f);

    const int rows = dialog_rows(modal);
    // Video's rows scroll in a window of five; the other dialogs show all of theirs.
    const bool scrolls = modal == Modal::video;
    const int first = scrolls ? video_rows_.first_row() : 0;
    const int last = scrolls ? video_rows_.last_row() : rows - 1;
    if (scrolls)
        list.push_clip({kVideoWindow.x - 24.0f, kVideoWindow.y - 6.0f, kVideoWindow.w + 48.0f,
                        kVideoWindow.h + 12.0f});
    for (int row = first; row <= last; ++row)
    {
        list.push_opacity(scrolls ? video_rows_.row_alpha(row, kVideoRowHeight) : 1.0f);
        plate_rest(c, kRowPlate, {592.0f, dialog_row_top(modal, row), 736.0f, 94.0f});
        list.pop_opacity();
    }
    plate_focus(c, kRowPlate,
                {592.0f,
                 scrolls ? kVideoRowsTop + video_rows_.cursor() - video_rows_.scroll() :
                           option_cursor_.value,
                 736.0f, 94.0f},
                1.0f);

    // A row's name takes what its control (`taken` wide, at the right) leaves of the row.
    const auto label = [&](int row, const char *value, float taken)
    {
        text_shrink(c, value, 628.0f, baseline(dialog_row_top(modal, row), 94.0f, theme::kText24),
                    theme::kText24, theme::kValue, 664.0f - taken - 28.0f);
    };
    const auto choice = [&](int row, const std::string &value)
    {
        return chooser(c, value, 1296.0f,
                       baseline(dialog_row_top(modal, row), 94.0f, theme::kText24),
                       row == option_ ? 1.0f : 0.0f, theme::kLimePale);
    };
    constexpr float kToggle = 64.0f;
    const auto row_centre = [&](int row) { return dialog_row_top(modal, row) + 47.0f; };
    const float knob = tween::clamp01(switches_[0].value);

    switch (modal)
    {
    case Modal::video:
    {
        const std::string values[] = {
            prefs_.renderer != 0 ? tr("Vulkan (recommended)") : "OpenGL",
            output_name(prefs_.output),
            pick(services_.resolution_labels(), prefs_.resolution),
            pick(services_.filter_labels(), prefs_.filter),
            hertz(prefs_.refresh),
        };
        static constexpr const char *kNames[kVideoRows] = {
            TR("Renderer"),         TR("Output resolution"), TR("Resolution"),
            TR("Upscaling filter"), TR("Refresh rate"),      TR("FPS overlay")};
        for (int row = first; row <= last; ++row)
        {
            list.push_opacity(video_rows_.row_alpha(row, kVideoRowHeight));
            if (row == video_overlay)
            {
                label(row, tr(kNames[row]), kToggle);
                toggle(c, 1292.0f, row_centre(row), knob);
            }
            else
            {
                label(row, tr(kNames[row]), choice(row, values[row]));
            }
            list.pop_opacity();
        }
        break;
    }
    case Modal::audio:
    {
        // A level row: its value at the right, the bar ending 20 before it (or where "100%"
        // would leave it), and the name in what remains.
        const auto level_row = [&](int row, const char *name, const std::string &value, int level)
        {
            const float shown =
                text(c, value, 1292.0f, baseline(dialog_row_top(modal, row), 94.0f, theme::kText24),
                     theme::kText24, theme::kLimePale, Align::right);
            const float gap = std::max(96.0f, shown + 20.0f);
            level_bar(c, 1292.0f - gap, row_centre(row), 260.0f, static_cast<float>(level) / 100.0f,
                      option_ == row ? 1.0f : 0.0f);
            label(row, name, 260.0f + gap);
        };
        level_row(0, tr("Game volume"), percent(prefs_.volume), prefs_.volume);
        label(1, tr("Mute"), kToggle);
        toggle(c, 1292.0f, row_centre(1), knob);
        level_row(2, tr("Menu sounds"), prefs_.menu_volume > 0 ? percent(prefs_.menu_volume) : tr("Off"),
                  prefs_.menu_volume);
        break;
    }
    case Modal::controls:
    {
        // Each shortcut: its buttons as a key cap, then what it does.
        struct Shortcut
        {
            const char *keys;
            const char *action;
        };
        static constexpr Shortcut kShortcuts[] = {
            {"Select + L1", TR("End the game and return to this menu")},
            {"Select + R1", TR("Show or hide the FPS overlay")}};
        for (int i = 0; i < 2; ++i)
        {
            const float top = 366.0f + 78.0f * static_cast<float>(i);
            list.bordered_rect({592.0f, top, 186.0f, 54.0f}, 12.0f, Color::rgb(0x15231d, 0.9f),
                               1.0f, theme::kRowEdge.with_alpha(0.6f));
            text(c, kShortcuts[i].keys, 685.0f, baseline(top, 54.0f, theme::kSmall), theme::kSmall,
                 theme::kLimePale, Align::center);
            text_shrink(c, tr(kShortcuts[i].action), 802.0f, baseline(top, 54.0f, 22.0f), 22.0f,
                        theme::kBody, 526.0f);
        }
        text_shrink(c, tr("Select is the touchpad button on PS5."), 592.0f,
                    baseline(540.0f, 36.0f, theme::kSmall), theme::kSmall, theme::kMeta, 736.0f);
        label(0, tr("Vibration"), kToggle);
        toggle(c, 1292.0f, row_centre(0), knob);
        break;
    }
    case Modal::accessibility:
    {
        static constexpr const char *kNames[] = {TR("Larger text"), TR("High contrast"),
                                                 TR("Reduce motion")};
        static constexpr const char *kAbout[] = {
            TR("Draws the menu's small text larger."),
            TR("Solid panels, brighter text and an outlined highlight."),
            TR("Stops the background drifting and the screens sliding, here and on the loading "
               "screen.")};
        for (int row = 0; row < 3; ++row)
        {
            label(row, tr(kNames[row]), kToggle);
            toggle(c, 1292.0f, row_centre(row),
                   tween::clamp01(switches_[static_cast<std::size_t>(row)].value));
        }
        // What the highlighted switch does.
        text_block(c, tr(kAbout[std::clamp(option_, 0, 2)]), 592.0f,
                   baseline(700.0f, 30.0f, theme::kSmall), theme::kSmall, 30.0f, theme::kMeta, 736.0f,
                   2, kShrink);
        break;
    }
    default:
        text_block(c, services_.setup_details(), 592.0f, baseline(364.0f, 40.0f, theme::kText24),
                   theme::kText24, 40.0f, theme::kBody, 736.0f, 7);
        label(0, tr("Detailed logging"), kToggle);
        toggle(c, 1292.0f, row_centre(0), knob);
        break;
    }

    if (scrolls)
    {
        list.pop_clip();
        scrollbar(c, video_rows_, 1340.0f, kVideoWindow.y, kVideoWindow.h);
    }

    // Under the rows: Video's five end lower than the other dialogs' three.
    const float foot = modal == Modal::video ? 848.0f : 811.0f;
    if (!message_.empty())
    {
        notice(c, message_, 592.0f, foot + 7.0f, theme::kSmall,
               message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f, message_warning_);
    }
    else if (rows > 1)
    {
        static constexpr Hint kHints[] = {
            {Pad::updown, TR("Select")}, {Pad::leftright, TR("Change")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, foot, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {{Pad::cross, TR("Change")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 2, 592.0f, foot, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

} // namespace pe::ui
