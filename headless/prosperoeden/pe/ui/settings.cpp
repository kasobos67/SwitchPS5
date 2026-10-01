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
constexpr const char *kCategories[] = {TR("Video"),       TR("Audio"),      TR("Controls"),
                                       TR("Diagnostics"), TR("Game files"), TR("Language")};
// The same as headings: capitals differ by language, so each is its own text.
constexpr const char *kHeadings[] = {TR("VIDEO"),       TR("AUDIO"),      TR("CONTROLS"),
                                     TR("DIAGNOSTICS"), TR("GAME FILES"), TR("LANGUAGE")};

const char *on_off(bool value)
{
    return value ? tr("On") : tr("Off");
}

std::string percent(int value)
{
    return std::to_string(value) + "%";
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
        if (settings_.selected == 4)
        {
            open(Screen::files, true);
            enter_files();
        }
        else if (settings_.selected == 5)
        {
            open(Screen::language, true);
            enter_language();
        }
        else
        {
            static constexpr Modal kModals[] = {Modal::video, Modal::audio, Modal::controls,
                                                Modal::diagnostics};
            open_modal(kModals[settings_.selected]);
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
    { return {150.0f, kRowsTop + settings_.pitch * static_cast<float>(row), 736.0f, 94.0f}; };
    for (int row = 0; row < 6; ++row)
        plate_rest(c, kRowPlate, row_rect(row));
    plate_focus(c, kRowPlate, {150.0f, kRowsTop + settings_.cursor(), 736.0f, 94.0f}, 1.0f);
    const std::string summaries[] = {
        prefs_.renderer != 0 ? "Vulkan" : "OpenGL",
        prefs_.mute ? tr("Muted") : percent(prefs_.volume),
        prefs_.vibration ? tr("Vibration on") : tr("Vibration off"),
        prefs_.detailed_logging ? tr("Detailed logs on") : "",
        "",
        pick(services_.language_labels(), prefs_.language),
    };
    for (int row = 0; row < 6; ++row)
    {
        const Rect r = row_rect(row);
        // What the category is set to, then a chevron: there is more behind the row.
        const float summary =
            text_shrink(c, summaries[row], r.x + r.w - 62.0f, baseline(r.y, 94.0f, theme::kSmall),
                        theme::kSmall, theme::kMeta, 330.0f, Align::right);
        text_shrink(c, tr(kCategories[row]), r.x + 36.0f, baseline(r.y, 94.0f, theme::kText24),
                    theme::kText24, theme::kValue, r.w - 36.0f - 62.0f - summary - 24.0f);
        const float cx = r.x + r.w - 34.0f;
        const float cy = r.y + 47.0f;
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
    case 0:
        about = tr("Graphics backend and how games are scaled to your TV.");
        lines = {{tr("RENDERER"), prefs_.renderer != 0 ? tr("Vulkan (recommended)") : "OpenGL"},
                 {tr("RESOLUTION"), pick(services_.resolution_labels(), prefs_.resolution)},
                 {tr("UPSCALING FILTER"), pick(services_.filter_labels(), prefs_.filter)},
                 {tr("FPS OVERLAY"), on_off(prefs_.hud)}};
        break;
    case 1:
        about = tr("Game volume, and the sounds of this menu.");
        lines = {{tr("GAME VOLUME"), percent(prefs_.volume)},
                 {tr("MUTE"), on_off(prefs_.mute)},
                 {tr("MENU SOUNDS"), prefs_.menu_volume > 0 ? percent(prefs_.menu_volume) : tr("Off")}};
        break;
    case 2:
        about = tr("Shortcuts during a game, and vibration.");
        lines = {{tr("VIBRATION"), on_off(prefs_.vibration)},
                 {tr("END GAME"), "Select + L1"},
                 {tr("FPS OVERLAY"), "Select + R1"}};
        break;
    case 3:
        about = tr("Setup status and detailed logs.");
        lines = {{tr("SETUP"), home_.setup_ready ? tr("Ready") : tr("Needs attention")},
                 {tr("DETAILED LOGS"), on_off(prefs_.detailed_logging)}};
        break;
    case 4:
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
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - shown) * 10.0f);
    text(c, tr(kHeadings[settings_.selected]), 1016.0f, baseline(458.0f, 30.0f, theme::kSmall),
         theme::kSmall, theme::kLime, Align::left, 3.0f);
    text_shrink(c, about, 1016.0f, baseline(494.0f, 32.0f, 22.0f), 22.0f, theme::kCopy, 748.0f);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const float top = 562.0f + 62.0f * static_cast<float>(i);
        const float value =
            text_fit(c, lines[i].value, 1764.0f, baseline(top, 36.0f, theme::kText24),
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
        return 4;
    case Modal::audio:
        return 3;
    case Modal::game:
        return services_.save_import_available() ? 5 : 4;
    default:
        return 1;
    }
}

float Launcher::dialog_row_top(Modal modal, int row) const
{
    switch (modal)
    {
    case Modal::video:
    case Modal::audio:
        return 370.0f + 102.0f * static_cast<float>(row);
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
        if (option_ == 0)
            prefs_.renderer = prefs_.renderer != 0 ? 0 : 1;
        else if (option_ == 1)
        {
            const int count = static_cast<int>(services_.resolution_labels().size());
            prefs_.resolution = (prefs_.resolution + step + count) % count;
        }
        else if (option_ == 2)
        {
            const int count = static_cast<int>(services_.filter_labels().size());
            prefs_.filter = (prefs_.filter + step + count) % count;
        }
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
    case Modal::diagnostics:
        prefs_.detailed_logging = !prefs_.detailed_logging;
        break;
    default:
        return;
    }
    if (!save_preferences())
    {
        prefs_ = before;
        sound = Cue::error;
    }
    cue(sound);
}

void Launcher::draw_dialog(Canvas &c, Modal modal, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(0.97f + 0.03f * open, 960.0f, 540.0f, 0.0f, (1.0f - open) * 26.0f);
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
    for (int row = 0; row < rows; ++row)
        plate_rest(c, kRowPlate, {592.0f, dialog_row_top(modal, row), 736.0f, 94.0f});
    plate_focus(c, kRowPlate, {592.0f, option_cursor_.value, 736.0f, 94.0f}, 1.0f);

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
    constexpr float kLevel = 356.0f; // the bar and the number beside it
    const auto row_centre = [&](int row) { return dialog_row_top(modal, row) + 47.0f; };
    const float knob = tween::clamp01(switches_[0].value);

    switch (modal)
    {
    case Modal::video:
        label(0, tr("Renderer"),
              choice(0, prefs_.renderer != 0 ? tr("Vulkan (recommended)") : "OpenGL"));
        label(1, tr("Resolution"), choice(1, pick(services_.resolution_labels(), prefs_.resolution)));
        label(2, tr("Upscaling filter"), choice(2, pick(services_.filter_labels(), prefs_.filter)));
        label(3, tr("FPS overlay"), kToggle);
        toggle(c, 1292.0f, row_centre(3), knob);
        break;
    case Modal::audio:
        label(0, tr("Game volume"), kLevel);
        text(c, percent(prefs_.volume), 1292.0f,
             baseline(dialog_row_top(modal, 0), 94.0f, theme::kText24), theme::kText24,
             theme::kLimePale, Align::right);
        level_bar(c, 1196.0f, row_centre(0), 260.0f, static_cast<float>(prefs_.volume) / 100.0f,
                  option_ == 0 ? 1.0f : 0.0f);
        label(1, tr("Mute"), kToggle);
        toggle(c, 1292.0f, row_centre(1), knob);
        label(2, tr("Menu sounds"), kLevel);
        text(c, prefs_.menu_volume > 0 ? percent(prefs_.menu_volume) : tr("Off"), 1292.0f,
             baseline(dialog_row_top(modal, 2), 94.0f, theme::kText24), theme::kText24,
             theme::kLimePale, Align::right);
        level_bar(c, 1196.0f, row_centre(2), 260.0f,
                  static_cast<float>(prefs_.menu_volume) / 100.0f, option_ == 2 ? 1.0f : 0.0f);
        break;
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
    default:
        text_block(c, services_.setup_details(), 592.0f, baseline(364.0f, 40.0f, theme::kText24),
                   theme::kText24, 40.0f, theme::kBody, 736.0f, 7);
        label(0, tr("Detailed logging"), kToggle);
        toggle(c, 1292.0f, row_centre(0), knob);
        break;
    }

    if (!message_.empty())
    {
        text_shrink(c, message_, 592.0f, 818.0f, theme::kSmall,
                    message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f);
    }
    else if (rows > 1)
    {
        static constexpr Hint kHints[] = {
            {Pad::updown, TR("Select")}, {Pad::leftright, TR("Change")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, 811.0f, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {{Pad::cross, TR("Change")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 2, 592.0f, 811.0f, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

} // namespace pe::ui
