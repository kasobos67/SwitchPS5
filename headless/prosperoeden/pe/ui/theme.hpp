// ProsperoEden - Launcher colours, type sizes and motion constants.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/gfx/draw_list.hpp"

#include <algorithm>

namespace pe::ui
{

using gfx::Align;
using gfx::Color;
using gfx::Rect;

namespace theme
{

// Backgrounds
inline Color kBase = Color::rgb(0x2d2d2d);
inline const Color kScrim = Color::rgb(0x000000);
inline Color kGlass = Color::rgb(0x353535);
inline Color kPanel = Color::rgb(0x2d2d2d);
inline Color kPanelEdge = Color::rgb(0x555555);
inline Color kRow = Color::rgb(0x2d2d2d);
inline Color kRowEdge = Color::rgb(0x515151);
inline Color kRowFocus = Color::rgb(0x414141);
inline Color kShortcut = Color::rgb(0x505050);

// The accent: Switch HOME turquoise focus.
inline const Color kLime = Color::rgb(0x30d5c8);
inline const Color kLimeDeep = Color::rgb(0x30d5c8);
inline const Color kLimePale = Color::rgb(0x55ded3);
inline const Color kSun = Color::rgb(0xffffff);

// Text
inline Color kText = Color::rgb(0xf5f5f5);
inline Color kTitle = Color::rgb(0xffffff);
inline Color kValue = Color::rgb(0xf5f5f5);
inline Color kBody = Color::rgb(0xeeeeee);
inline Color kCopy = Color::rgb(0xcfcfcf);
inline Color kMeta = Color::rgb(0xbdbdbd);
inline Color kLabel = Color::rgb(0xbdbdbd);
inline Color kMuted = Color::rgb(0xb6b6b6);
inline Color kFaint = Color::rgb(0x999999);
inline const Color kWarning = Color::rgb(0xffb36a);
inline Color kRule = Color::rgb(0x555555);

inline void set_white(bool white)
{
    kBase = kPanel = kRow = Color::rgb(white ? 0xebebeb : 0x2d2d2d);
    kGlass = kRowFocus = Color::rgb(white ? 0xffffff : 0x414141);
    kShortcut = Color::rgb(white ? 0xffffff : 0x505050);
    kPanelEdge = kRowEdge = kRule = Color::rgb(white ? 0xc7c7c7 : 0x555555);
    kText = kTitle = kValue = Color::rgb(white ? 0x252525 : 0xf5f5f5);
    kBody = Color::rgb(white ? 0x303030 : 0xeeeeee);
    kCopy = kMeta = kLabel = kMuted = Color::rgb(white ? 0x646464 : 0xbdbdbd);
    kFaint = Color::rgb(white ? 0x888888 : 0x999999);
}

// Type sizes (Montserrat Medium)
constexpr float kDisplay = 48.0f;
constexpr float kLead = 40.0f;
constexpr float kBrand = 36.0f;
constexpr float kHeading = 32.0f;
constexpr float kClock = 28.0f;
constexpr float kText24 = 24.0f;
constexpr float kSmall = 20.0f;

// Motion (spring responsiveness in rad/s, durations in seconds)
constexpr float kFocusSpring = 20.0f;
constexpr float kCursorSpring = 26.0f;
constexpr float kScrollSpring = 16.0f;
constexpr float kScreenSeconds = 0.22f;
constexpr float kLaunchSeconds = 0.95f;

} // namespace theme

// How the launcher is shown: Settings > Accessibility.
struct Look
{
    bool large_text = false;    // small text is drawn larger
    bool high_contrast = false; // solid panels, brighter text, an outlined highlight
    bool reduce_motion = false; // nothing drifts, slides or zooms: screens fade
    bool white_theme = false;
};
inline Look &look()
{
    static Look value;
    return value;
}
// 1 normally, 0 with reduced motion: every slide, zoom and drift is multiplied by it.
inline float motion()
{
    return look().reduce_motion ? 0.0f : 1.0f;
}
// The size text of `size` is drawn at. "Larger text" raises everything under 30 to between 26
// and 30 (26 is the least the console accessibility guidelines ask of text on a 1080p TV).
inline float text_size(float size)
{
    return look().large_text && size < 30.0f ? std::clamp(size * 1.3f, 26.0f, 30.0f) : size;
}

// The baseline that centres text of `size` in a line box of height `line` starting at `top`.
inline float baseline(float top, float line, float size)
{
    return top + line * 0.5f + size * 0.35f;
}

inline bool inside(const Rect &r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

} // namespace pe::ui
