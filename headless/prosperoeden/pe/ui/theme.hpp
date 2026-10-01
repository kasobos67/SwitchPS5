// ProsperoEden - Launcher colours, type sizes and motion constants.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/gfx/draw_list.hpp"

namespace pe::ui
{

using gfx::Align;
using gfx::Color;
using gfx::Rect;

namespace theme
{

// Backgrounds
inline const Color kBase = Color::rgb(0x06090a);
inline const Color kScrim = Color::rgb(0x010806);
inline const Color kGlass = Color::rgb(0x16221d);  // home panels
inline const Color kPanel = Color::rgb(0x0b1713);  // screens and dialogs
inline const Color kPanelEdge = Color::rgb(0x768e75);
inline const Color kRow = Color::rgb(0x17241e);
inline const Color kRowEdge = Color::rgb(0x6a8267);
inline const Color kRowFocus = Color::rgb(0x294433);

// The accent: lime to deep green, left to right.
inline const Color kLime = Color::rgb(0xa9db63);
inline const Color kLimeDeep = Color::rgb(0x245d4a);
inline const Color kLimePale = Color::rgb(0xdfe8a6);
inline const Color kSun = Color::rgb(0xffd76a);

// Text
inline const Color kText = Color::rgb(0xf0f5f2);
inline const Color kTitle = Color::rgb(0xfffcef);
inline const Color kValue = Color::rgb(0xf2f4e9);
inline const Color kBody = Color::rgb(0xe2e8d7);
inline const Color kCopy = Color::rgb(0xc5d0bd);
inline const Color kMeta = Color::rgb(0xb7c6b5);
inline const Color kLabel = Color::rgb(0xaebdaa);
inline const Color kMuted = Color::rgb(0xaab8ac);
inline const Color kFaint = Color::rgb(0x8c9e90);
inline const Color kWarning = Color::rgb(0xe8b39a);
inline const Color kRule = Color::rgb(0x536b55);

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
constexpr float kScreenSeconds = 0.30f;
constexpr float kLaunchSeconds = 0.95f;

} // namespace theme

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
