// SPDX-License-Identifier: GPL-3.0-or-later
// The output's refresh rate for a game session: what Settings > Video (or the game's own
// settings) asks for, and what the display took. 120 Hz needs the package to declare it
// (tools/package-headless-native.sh), a display that shows it and the console's own 120 Hz output
// setting; otherwise the session presents at 60 Hz. The launcher always runs at 60 Hz.
#pragma once
#include <atomic>

namespace Eden::Display {
// 60 or 120, set before a session's renderer starts.
inline std::atomic<int> requested_hz{60};
// What the renderer's output runs at, in millihertz (59940, 119880); 0 before it opened.
inline std::atomic<int> output_millihertz{0};
// The Vulkan driver's display code reads this when it opens the output (tools/patch-radv-wsi.py).
inline constexpr const char* kVulkanSwitch = "EDEN_VIDEOOUT_120HZ";
// After a session at 120 Hz the output goes back to 60 Hz, and the display follows. The next
// presenter (the launcher's) opens after this long, as the OpenGL SDK does by itself for its own
// 120 Hz sessions (its docs/lifecycle-reopen.md). Set by the Vulkan surface that took 120 Hz.
inline std::atomic<bool> settle{false};
inline constexpr int kSettleSeconds = 5;
} // namespace Eden::Display
