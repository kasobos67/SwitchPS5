// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace Eden {
// Counts contiguous presentation intervals; loading gaps are never reported as FPS.
struct HudClock {
    double start{-1}, last{-1}, fps{-1}, worst_ms{}, window_worst_ms{};
    unsigned frames{};
    void Present(double now) {
        if (last >= 0 && now - last > 0.5) {
            start = last = now;
            fps = -1;
            worst_ms = 0;
            window_worst_ms = 0;
            frames = 0;
            return;
        }
        if (last >= 0) window_worst_ms = std::max(window_worst_ms, (now - last) * 1000.0);
        last = now;
        if (start < 0) { start = now; return; }
        ++frames;
        if (now - start >= 1.0) {
            fps = frames / (now - start);
            worst_ms = window_worst_ms;
            window_worst_ms = 0;
            frames = 0;
            start = now;
        }
    }
};

// Keep startup work behind the loading screen until presentation is sustained.
struct StartupGate {
    double last{-1}, first{-1};
    unsigned smooth_frames{};
    bool Ready(double now) {
        if (first < 0) first = now;
        smooth_frames = last >= 0 && now > last && now - last <= 0.1 ? smooth_frames + 1 : 0;
        last = now;
        return smooth_frames >= 8 || now - first >= 5.0;
    }
};
inline uint32_t HudGlyph(char c) {
    switch (c) {
    case '0': return 0x7b6f;
    case '1': return 0x2c97;
    case '2': return 0x73e7;
    case '3': return 0x73cf;
    case '4': return 0x5bc9;
    case '5': return 0x79cf;
    case '6': return 0x79ef;
    case '7': return 0x7292;
    case '8': return 0x7bef;
    case '9': return 0x7bcf;
    case 'F': return 0x79a4;
    case 'P': return 0x6ba4;
    case 'S': return 0x79cf;
    case 'L': return 0x4927;
    case 'O': return 0x7b6f;
    case 'A': return 0x2bed;
    case 'D': return 0x6b6e;
    case 'I': return 0x7497;
    case 'N': return 0x5ffd;
    case 'G': return 0x796f;
    case 'V': return 0x5b6a;
    case 'K': return 0x5bad;
    case 'W': return 0x5fed;
    case '.': return 0x0002;
    case '-': return 0x01c0;
    default: return 0;
    }
}
inline std::array<uint32_t, 24> HudText(std::string_view text) {
    std::array<uint32_t, 24> glyphs{};
    for (size_t i = 0; i < text.size() && i < glyphs.size(); ++i)
        glyphs[i] = HudGlyph(text[i]);
    return glyphs;
}
struct HudSnapshot {
    std::array<uint32_t, 24> glyphs{};
    uint32_t width{};
    uint32_t x{28}, y{30}, loading{};
};
inline HudSnapshot MakeLoadingSnapshot(double now) {
    char text[16] = "LOADING";
    for (unsigned i = 0; i < static_cast<unsigned>(now * 4) % 4; ++i) text[7 + i] = '.';
    return {HudText(text), 1920, 880, 530, 1};
}
inline std::array<char, 25> FormatHudText(const HudClock& clock, double speed,
                                          const char* backend) {
    std::array<char, 25> text{};
    if (clock.fps < 0) std::snprintf(text.data(), text.size(), "%s F-- S-- W--", backend);
    else std::snprintf(text.data(), text.size(), "%s F%.0f S%.0f W%.0f",
                       backend, clock.fps, speed, clock.worst_ms);
    return text;
}
inline HudSnapshot MakeHudSnapshot(const HudClock& clock, double speed) {
    const auto text = FormatHudText(clock, speed, "VLK");
    const std::string_view value{text.data()};
    return {HudText(value), static_cast<uint32_t>(value.size() * 16 + 24)};
}
// Read on the renderer thread; the scheduler captures the returned value per frame.
HudSnapshot GetVulkanHud();
} // namespace Eden
