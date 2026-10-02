// SPDX-License-Identifier: GPL-3.0-or-later
// The output's refresh rate for a game session: what Settings > Video (or the game's own
// settings) asks for, and what the display took. 120 Hz needs the package to declare it
// (attribute3 in sce_sys/param.json), a display that shows it and the console's own 120 Hz output
// setting; otherwise the session presents at 60 Hz. The launcher always runs at 60 Hz.
// Also the size of the picture a session puts out.
#pragma once
#include <atomic>
#include <chrono>

namespace Eden::Display {
// Settings > Video > Output resolution: 1920x1080, 2560x1440 or 3840x2160, set before a session's
// renderer starts. It is the size of the game's frame after its upscaling filter. The OpenGL
// renderer's surface has that size and the console scales it to the TV; the Vulkan renderer's
// frame has that size and is copied to the driver's 3840x2160 output (scaled when it is smaller).
inline std::atomic<int> output_width{1920};
inline std::atomic<int> output_height{1080};
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

// The rate the emulator's vsync clock gives the game, in millihertz: 60000 for a game that swaps
// every vsync, 30000 for every second one, and what an FPS patch asks for (120000, 240000: a swap
// interval of 0, or a percentage of 60 Hz). Kept by the clock itself (Eden's vi/conductor.cpp,
// derived in CMakeLists.txt).
inline std::atomic<int> game_millihertz{60000};
// Frames left out since the session began (SkipFrame).
inline std::atomic<unsigned> skipped_frames{0};

// Whether the frame the game just made is left out. True only when the game's clock runs faster
// than the output refreshes (240 FPS on a 120 Hz output, 120 FPS on a 60 Hz one) and the last
// frame shown is less than three quarters of a refresh old. The display then shows every second
// or fourth frame, and the game keeps its pace instead of waiting for a refresh per frame. A game
// that is slower than its clock loses nothing: its frames are further apart than that.
// The renderer's thread calls it, once per frame.
inline bool SkipFrame() {
    const int output = output_millihertz.load(std::memory_order_relaxed);
    const int game = game_millihertz.load(std::memory_order_relaxed);
    if (output <= 0 || game <= output + output / 20) return false;
    static long long last_shown_ns = 0;
    const long long now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const long long refresh_ns = 1'000'000'000'000LL / output;
    if (now_ns - last_shown_ns < refresh_ns * 3 / 4) {
        skipped_frames.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    last_shown_ns = now_ns;
    return false;
}
} // namespace Eden::Display
