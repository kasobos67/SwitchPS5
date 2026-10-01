// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <istream>
#include <optional>
#include "ps5_pad.hpp"

namespace Eden {
// Development-only file commands: "id mask duration [lx ly rx ry]" (sticks 0-255, 128 is
// centre; absent means centred). A pulse expires even if its sender disappears.
//
// A command fires once in the process: the launcher and every game session read the same file
// with a reader of their own, and a file a run left behind once ended every game its owner
// started (it held the "end game" shortcut). IgnoreExisting, called when the process starts,
// counts what is already in the file as taken.
struct DevelopmentInput {
    static inline std::atomic<std::uint64_t> taken{0};
    static void IgnoreExisting(std::istream& input) {
        std::uint64_t id{};
        if (input >> id && id > taken.load(std::memory_order_relaxed)) taken.store(id, std::memory_order_relaxed);
    }

    std::uint64_t sequence{}, until{};
    std::uint32_t buttons{};
    ps5::pad::Stick left{128, 128}, right{128, 128};
    bool active{};
    bool Read(std::istream& input, std::uint64_t now) {
        std::uint64_t id{}, mask{}, duration{};
        if (!(input >> id >> mask >> duration) || id <= taken.load(std::memory_order_relaxed) ||
            (mask & ~std::uint64_t(0x10FFFF)) ||
            duration == 0 || duration > 3000) return false;
        unsigned lx = 128, ly = 128, rx = 128, ry = 128;
        if (!(input >> lx >> ly >> rx >> ry)) {
            lx = ly = rx = ry = 128;
        } else if (lx > 255 || ly > 255 || rx > 255 || ry > 255) {
            return false;
        }
        taken.store(id, std::memory_order_relaxed);
        sequence = id;
        buttons = static_cast<std::uint32_t>(mask);
        left = {static_cast<std::uint8_t>(lx), static_cast<std::uint8_t>(ly)};
        right = {static_cast<std::uint8_t>(rx), static_cast<std::uint8_t>(ry)};
        until = now + duration;
        active = true;
        return true;
    }
    std::optional<ps5::pad::Data> Sample(std::uint64_t now) {
        if (!active) return {};
        auto sample = ps5::pad::neutral_data();
        sample.connected = 1;
        if (now < until) {
            sample.buttons = buttons;
            sample.left_stick = left;
            sample.right_stick = right;
        } else {
            active = false; // One final neutral sample releases the pulse.
        }
        return sample;
    }
};
}
