// ProsperoEden - Launcher building blocks: backdrop, panels, plates, lists and hints.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace pe::ui
{

namespace
{

constexpr float kTau = 6.28318530718f;
const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};
const Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};

// A repeatable 0..1 value for element `index` and property `salt`.
float noise(int index, int salt)
{
    std::uint32_t value = static_cast<std::uint32_t>(index) * 0x9e3779b1u +
                          static_cast<std::uint32_t>(salt) * 0x85ebca6bu + 0x27d4eb2fu;
    value ^= value >> 15;
    value *= 0x2c1b3c6du;
    value ^= value >> 12;
    value *= 0x297a2d39u;
    value ^= value >> 15;
    return static_cast<float>(value >> 8) / 16777216.0f;
}

} // namespace

// ---------------------------------------------------------------- backdrop

void Backdrop::update(float dt)
{
    time_ += dt;
}

Rect Backdrop::art() const
{
    // The art is 2048x1152 around the 1920x1080 screen: room to drift.
    const float dx = 24.0f * std::sin(time_ * 0.050f);
    const float dy = 11.0f * std::sin(time_ * 0.037f + 1.3f);
    return {-64.0f + dx, -36.0f + dy, 2048.0f, 1152.0f};
}

Rect Backdrop::uv(const Rect &r) const
{
    const Rect a = art();
    return {(r.x - a.x) / a.w, (r.y - a.y) / a.h, r.w / a.w, r.h / a.h};
}

void Backdrop::draw(gfx::DrawList &list, const Textures &textures, float dim) const
{
    const Rect screen{0.0f, 0.0f, 1920.0f, 1080.0f};
    list.rounded_rect(screen, 0.0f, theme::kBase);
    const Rect a = art();
    if (textures.backdrop() != 0)
        list.image(textures.backdrop(), a, {0.0f, 0.0f, 1.0f, 1.0f}, kWhite);

    // The sun breathes.
    const float sun_x = a.x + 0.710f * a.w;
    const float sun_y = a.y + 0.391f * a.h;
    const float breath = 0.5f + 0.5f * std::sin(time_ * 0.55f);
    list.shadow({sun_x - 150.0f, sun_y - 150.0f, 300.0f, 300.0f}, 150.0f, 170.0f,
                theme::kSun.with_alpha(0.03f + 0.04f * breath));

    // Motes of light rise over the water, nearer than the art.
    constexpr int kMotes = 26;
    constexpr float kSpan = 640.0f;
    const float near_x = (a.x + 64.0f) * 1.7f;
    const float near_y = (a.y + 36.0f) * 1.7f;
    for (int i = 0; i < kMotes; ++i)
    {
        const float speed = 6.0f + 11.0f * noise(i, 2);
        const float travel = std::fmod(time_ * speed + noise(i, 3) * kSpan, kSpan);
        const float life = std::sin(3.14159265f * travel / kSpan);
        const float x = 800.0f + 1120.0f * noise(i, 1) + near_x +
                        28.0f * std::sin(time_ * (0.21f + 0.2f * noise(i, 4)) + kTau * noise(i, 5));
        const float y = 900.0f - travel + near_y;
        const float size = 3.0f + 5.0f * noise(i, 6);
        const float twinkle =
            0.65f + 0.35f * std::sin(time_ * (0.9f + 1.4f * noise(i, 7)) + kTau * noise(i, 8));
        const float alpha = 0.30f * life * twinkle;
        const Color color = gfx::mix(theme::kLimePale, theme::kSun, noise(i, 9));
        list.shadow({x - size, y - size, size * 2.0f, size * 2.0f}, size, size * 0.9f,
                    color.with_alpha(alpha));
        list.circle(x, y, size * 0.28f, color.with_alpha(std::min(1.0f, alpha * 1.8f)));
    }
    if (dim > 0.0f)
        list.rounded_rect(screen, 0.0f, theme::kScrim.with_alpha(dim));
}

// ---------------------------------------------------------------- text

float text(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
           Align align, float tracking)
{
    gfx::TextStyle style;
    style.size = size;
    style.color = color;
    style.align = align;
    style.tracking = tracking;
    return c.list.text(*c.fonts.font, c.fonts.texture, value, x, baseline, style);
}

float text_fit(Canvas &c, std::string_view value, float x, float baseline, float size, Color color,
               float max_width, Align align)
{
    return text(c, c.fonts.font->fit(value, size, max_width), x, baseline, size, color, align);
}

void text_block(Canvas &c, std::string_view value, float x, float first_baseline, float size,
                float line_height, Color color, float max_width, int max_lines)
{
    const std::vector<std::string> lines = c.fonts.font->wrap(value, size, max_width);
    const int count = std::min(static_cast<int>(lines.size()), max_lines);
    for (int line = 0; line < count; ++line)
    {
        const bool cut = line == count - 1 && static_cast<int>(lines.size()) > max_lines;
        // The last line that fits swallows the next one, so its ellipsis shows more follows.
        const std::string content = cut ? lines[line] + " " + lines[line + 1] : lines[line];
        text_fit(c, content, x, first_baseline + line_height * static_cast<float>(line), size,
                 color, max_width);
    }
}

// ---------------------------------------------------------------- surfaces

void glass(Canvas &c, const Rect &r, float radius, Color tint, Color edge, float shadow)
{
    if (shadow > 0.0f)
        c.list.shadow({r.x + 6.0f, r.y + 20.0f, r.w - 12.0f, r.h - 8.0f}, radius, 48.0f,
                      kBlack.with_alpha(0.42f * shadow));
    if (c.textures.backdrop_blur() != 0)
        c.list.rounded_image(c.textures.backdrop_blur(), r, c.backdrop.uv(r), radius, kWhite);
    c.list.bordered_rect(r, radius, tint, 1.0f, edge);
    // Light catches the top edge.
    const float sheen = std::min(r.h * 0.45f, 150.0f);
    c.list.gradient_rect({r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, sheen}, radius,
                         kWhite.with_alpha(0.045f), kWhite.with_alpha(0.0f));
}

const Plate kRowPlate{15.0f,
                      theme::kRow.with_alpha(0.90f),
                      theme::kRowEdge.with_alpha(0.47f),
                      theme::kRowFocus.with_alpha(0.95f),
                      theme::kLime.with_alpha(0.44f),
                      theme::kLimeDeep.with_alpha(0.50f),
                      theme::kLime.with_alpha(0.63f)};
const Plate kListPlate{13.0f,
                       Color::rgb(0x15231d, 0.91f),
                       Color::rgb(0x688267, 0.47f),
                       Color::rgb(0x2a4434, 0.95f),
                       theme::kLime.with_alpha(0.44f),
                       theme::kLimeDeep.with_alpha(0.50f),
                       theme::kLime.with_alpha(0.63f)};
const Plate kButtonPlate{12.0f,
                         Color::rgb(0xe6ede4, 0.10f),
                         kWhite.with_alpha(0.19f),
                         Color::rgb(0x1c2c24, 0.55f),
                         theme::kLime.with_alpha(0.50f),
                         theme::kLimeDeep.with_alpha(0.44f),
                         theme::kLime.with_alpha(0.63f)};
const Plate kTilePlate{12.0f,
                       Color::rgb(0xeef7ed, 0.06f),
                       kWhite.with_alpha(0.125f),
                       Color::rgb(0x1c2c24, 0.45f),
                       theme::kLime.with_alpha(0.345f),
                       theme::kLimeDeep.with_alpha(0.376f),
                       theme::kLime.with_alpha(0.63f)};
const Plate kNavPlate{12.0f,
                      kWhite.with_alpha(0.0f),
                      kWhite.with_alpha(0.0f),
                      Color::rgb(0x1c2c24, 0.0f),
                      theme::kLime.with_alpha(0.31f),
                      theme::kLimeDeep.with_alpha(0.31f),
                      theme::kLime.with_alpha(0.50f)};

void plate_rest(Canvas &c, const Plate &style, const Rect &r)
{
    if (style.fill.a > 0.0f || style.edge.a > 0.0f)
        c.list.bordered_rect(r, style.radius, style.fill, 1.0f, style.edge);
}

void plate_focus(Canvas &c, const Plate &style, const Rect &r, float amount)
{
    if (amount <= 0.001f)
        return;
    // The highlight glows, breathing slowly.
    const float glow = 0.17f + 0.07f * std::sin(c.time * 2.6f);
    c.list.shadow({r.x - 2.0f, r.y + 2.0f, r.w + 4.0f, r.h + 2.0f}, style.radius + 2.0f, 26.0f,
                  theme::kLime.with_alpha(glow * amount));
    if (style.focus_base.a > 0.0f)
        c.list.rounded_rect(r, style.radius, style.focus_base.with_alpha(amount));
    c.list.hgradient_rect(r, style.radius, style.focus_left.with_alpha(amount),
                          style.focus_right.with_alpha(amount), 1.5f,
                          style.focus_edge.with_alpha(amount));
}

void plate(Canvas &c, const Plate &style, const Rect &r, float focus)
{
    plate_rest(c, style, r);
    plate_focus(c, style, r, focus);
}

void cover(Canvas &c, const std::string &path, const Rect &r, float radius, float shadow)
{
    if (shadow > 0.0f)
        c.list.shadow({r.x + 3.0f, r.y + 10.0f * shadow, r.w - 6.0f, r.h - 4.0f}, radius,
                      24.0f * shadow, kBlack.with_alpha(0.5f));
    const Cover image = c.textures.cover(path, r.w);
    const float fade = image.texture != 0 ? tween::cubic_out(image.age / 0.22f) : 0.0f;
    if (fade < 1.0f)
    {
        // A dark tile until the cover is ready; the app icon when the game has none.
        c.list.gradient_rect(r, radius, Color::rgb(0x16241d), Color::rgb(0x0c1511));
        if (image.missing && c.textures.brand() != 0)
            c.list.rounded_image(c.textures.brand(), r, {0.0f, 0.0f, 1.0f, 1.0f}, radius,
                                 kWhite.with_alpha(0.92f));
    }
    if (image.texture != 0)
        c.list.rounded_image(image.texture, r, {0.0f, 0.0f, 1.0f, 1.0f}, radius,
                             kWhite.with_alpha(fade));
    c.list.bordered_rect(r, radius, kWhite.with_alpha(0.0f), 1.0f, kWhite.with_alpha(0.10f));
}

void toggle(Canvas &c, float right, float cy, float position)
{
    constexpr float kWidth = 64.0f;
    constexpr float kHeight = 34.0f;
    const Rect track{right - kWidth, cy - kHeight * 0.5f, kWidth, kHeight};
    c.list.bordered_rect(track, kHeight * 0.5f, Color::rgb(0x24332c, 0.95f), 1.0f,
                         theme::kRowEdge.with_alpha(0.7f));
    c.list.hgradient_rect(track, kHeight * 0.5f, theme::kLime.with_alpha(0.95f * position),
                          Color::rgb(0x6fae52, 0.95f * position));
    const float knob_x = track.x + kHeight * 0.5f + (kWidth - kHeight) * position;
    c.list.shadow({knob_x - 12.0f, cy - 10.0f, 24.0f, 24.0f}, 12.0f, 6.0f, kBlack.with_alpha(0.35f));
    c.list.circle(knob_x, cy, 12.5f, gfx::mix(theme::kCopy, theme::kTitle, position));
}

void level_bar(Canvas &c, float right, float cy, float width, float level, float focus)
{
    constexpr float kHeight = 8.0f;
    const Rect track{right - width, cy - kHeight * 0.5f, width, kHeight};
    c.list.rounded_rect(track, kHeight * 0.5f, Color::rgb(0x2b3b33, 0.95f));
    const float filled = std::max(kHeight, width * std::clamp(level, 0.0f, 1.0f));
    if (level > 0.0f)
        c.list.hgradient_rect({track.x, track.y, filled, kHeight}, kHeight * 0.5f, theme::kLimeDeep,
                              theme::kLime);
    const float knob = 7.0f + 3.0f * focus;
    c.list.shadow({track.x + filled - knob, cy - knob + 2.0f, knob * 2.0f, knob * 2.0f}, knob, 5.0f,
                  kBlack.with_alpha(0.35f));
    c.list.circle(track.x + (level > 0.0f ? filled : 0.0f), cy, knob, theme::kTitle);
}

void chooser(Canvas &c, std::string_view value, float right, float baseline, float focus,
             Color color)
{
    const float size = theme::kText24;
    const float arrow = 26.0f * focus; // room the chevrons take when focused
    const float width = text(c, value, right - arrow, baseline, size, color, Align::right);
    if (focus <= 0.01f)
        return;
    const float cy = baseline - size * 0.35f;
    const Color ink = theme::kLimePale.with_alpha(focus);
    const float rx = right - 4.0f;
    c.list.line(rx - 7.0f, cy - 8.0f, rx, cy, 2.2f, ink);
    c.list.line(rx, cy, rx - 7.0f, cy + 8.0f, 2.2f, ink);
    const float lx = right - arrow - width - 16.0f;
    c.list.line(lx + 7.0f, cy - 8.0f, lx, cy, 2.2f, ink);
    c.list.line(lx, cy, lx + 7.0f, cy + 8.0f, 2.2f, ink);
}

// ---------------------------------------------------------------- controller hints

float pad_width(Pad button, float size)
{
    switch (button)
    {
    case Pad::none:
        return 0.0f;
    case Pad::l1:
    case Pad::r1:
        return size * 1.45f;
    default:
        return size;
    }
}

void draw_pad(Canvas &c, Pad button, float x, float cy, float size, float alpha)
{
    const float width = pad_width(button, size);
    const float cx = x + width * 0.5f;
    const float half = size * 0.5f;
    const Color ring = theme::kText.with_alpha(0.40f * alpha);
    const Color ink = theme::kText.with_alpha(0.92f * alpha);
    const Color dim = theme::kText.with_alpha(0.30f * alpha);
    const float stroke = size * 0.085f;
    switch (button)
    {
    case Pad::none:
        return;
    case Pad::cross:
    {
        const float d = size * 0.17f;
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.line(cx - d, cy - d, cx + d, cy + d, stroke, ink);
        c.list.line(cx - d, cy + d, cx + d, cy - d, stroke, ink);
        return;
    }
    case Pad::circle:
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.ring(cx, cy, size * 0.21f, stroke, ink);
        return;
    case Pad::square:
    {
        const float side = size * 0.36f;
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.bordered_rect({cx - side * 0.5f, cy - side * 0.5f, side, side}, 1.5f,
                             ink.with_alpha(0.0f), stroke, ink);
        return;
    }
    case Pad::triangle:
    {
        const float w = size * 0.46f;
        const float h = size * 0.40f;
        c.list.ring(cx, cy, half - 0.5f, 1.6f, ring);
        c.list.triangle({cx - w * 0.5f, cy - h * 0.60f, w, h}, ink, stroke);
        return;
    }
    case Pad::dpad:
    case Pad::updown:
    case Pad::leftright:
    {
        const float arm = size * 0.36f;
        const float thick = size * 0.26f;
        const Color vertical = button == Pad::leftright ? dim : ink;
        const Color horizontal = button == Pad::updown ? dim : ink;
        c.list.rounded_rect({cx - arm, cy - thick * 0.5f, arm * 2.0f, thick}, thick * 0.28f,
                            horizontal);
        c.list.rounded_rect({cx - thick * 0.5f, cy - arm, thick, arm * 2.0f}, thick * 0.28f,
                            vertical);
        if (button != Pad::dpad)
        {
            // Redraw the bright arm over the crossing so it reads as one piece.
            if (button == Pad::updown)
                c.list.rounded_rect({cx - thick * 0.5f, cy - arm, thick, arm * 2.0f},
                                    thick * 0.28f, ink);
            else
                c.list.rounded_rect({cx - arm, cy - thick * 0.5f, arm * 2.0f, thick},
                                    thick * 0.28f, ink);
        }
        return;
    }
    case Pad::l1:
    case Pad::r1:
    {
        const float height = size * 0.80f;
        c.list.bordered_rect({x, cy - height * 0.5f, width, height}, height * 0.30f,
                             ink.with_alpha(0.0f), 1.6f, ring);
        text(c, button == Pad::l1 ? "L1" : "R1", cx, cy + size * 0.17f, size * 0.48f, ink,
             Align::center);
        return;
    }
    }
}

float draw_hints(Canvas &c, const Hint *hints, int count, float x, float cy, Color color)
{
    constexpr float kSize = 28.0f;
    constexpr float kIconGap = 10.0f;
    constexpr float kPairGap = 6.0f;
    constexpr float kItemGap = 34.0f;
    float cursor = x;
    for (int i = 0; i < count; ++i)
    {
        const Hint &hint = hints[i];
        draw_pad(c, hint.button, cursor, cy, kSize);
        cursor += pad_width(hint.button, kSize);
        if (hint.second != Pad::none)
        {
            cursor += kPairGap;
            draw_pad(c, hint.second, cursor, cy, kSize);
            cursor += pad_width(hint.second, kSize);
        }
        cursor += kIconGap;
        cursor += text(c, hint.label, cursor, cy + theme::kSmall * 0.35f, theme::kSmall, color);
        if (i + 1 < count)
            cursor += kItemGap;
    }
    return cursor - x;
}

// ---------------------------------------------------------------- lists

void ListView::follow()
{
    const int max_top = std::max(0, count - visible);
    if (selected < top_)
        top_ = selected;
    if (selected > top_ + visible - 1)
        top_ = selected - (visible - 1);
    top_ = std::clamp(top_, 0, max_top);
    scroll_.target = static_cast<float>(top_) * pitch;
    cursor_.target = static_cast<float>(selected) * pitch;
}

void ListView::reset(int new_count, int new_selected)
{
    count = std::max(0, new_count);
    selected = std::clamp(new_selected, 0, std::max(0, count - 1));
    // Open with the selection a few rows down when the list is long enough.
    top_ = std::max(0, selected - visible / 2);
    follow();
    scroll_.snap(scroll_.target);
    cursor_.snap(cursor_.target);
}

bool ListView::move(int delta)
{
    if (count <= 0)
        return false;
    int next = selected + delta;
    if (wrap)
        next = (next % count + count) % count;
    else
        next = std::clamp(next, 0, count - 1);
    if (next == selected)
        return false;
    selected = next;
    follow();
    return true;
}

bool ListView::page(int delta)
{
    if (count <= 0)
        return false;
    const int next = std::clamp(selected + delta * visible, 0, count - 1);
    if (next == selected)
        return false;
    selected = next;
    follow();
    return true;
}

void ListView::update(float dt)
{
    scroll_.update(dt, theme::kScrollSpring);
    cursor_.update(dt, theme::kCursorSpring);
}

int ListView::first_row() const
{
    return std::max(0, static_cast<int>(std::floor(scroll_.value / pitch)));
}

int ListView::last_row() const
{
    return std::min(count - 1, static_cast<int>(std::floor(scroll_.value / pitch)) + visible);
}

float ListView::row_alpha(int row, float row_height) const
{
    const float y = static_cast<float>(row) * pitch - scroll_.value;
    const float window = static_cast<float>(visible - 1) * pitch + row_height;
    const float above = tween::clamp01((y + row_height) / row_height);
    const float below = tween::clamp01((window - y) / row_height);
    return std::min(above, below);
}

float ListView::thumb() const
{
    const float range = static_cast<float>(std::max(1, count - visible)) * pitch;
    return tween::clamp01(scroll_.value / range);
}

void scrollbar(Canvas &c, const ListView &view, float x, float y, float height)
{
    if (view.count <= view.visible)
        return;
    c.list.rounded_rect({x, y, 10.0f, height}, 5.0f, Color::rgb(0x49624a, 0.53f));
    const float thumb = std::max(
        64.0f, height * static_cast<float>(view.visible) / static_cast<float>(view.count));
    c.list.rounded_rect({x, y + (height - thumb) * view.thumb(), 10.0f, thumb}, 5.0f,
                        Color::rgb(0xd8e8aa));
}

} // namespace pe::ui
