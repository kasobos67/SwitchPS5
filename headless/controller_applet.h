// SPDX-License-Identifier: GPL-3.0-or-later
// The guest controller applet (a game's "connect controllers" screen) without a UI. Eden's default
// connects only the game's minimum player count, which disconnects a second player whenever a game
// asks; this connects one player per PS5 controller in use, within the game's limits, with the
// same controller style priority (Pro Controller, dual Joy-Con, single Joy-Con, handheld).
#pragma once
#include <algorithm>
#include <bit>
#include "common/settings.h"
#include "core/frontend/applets/controller.h"
#include "devices.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"

namespace Eden {
class PadControllerApplet final : public Core::Frontend::ControllerApplet {
public:
    PadControllerApplet(Core::HID::HIDCore& hid_core_, const Pad& pad_) : hid_core{hid_core_}, pad{pad_} {}

    void Close() const override {}

    void ReconfigureControllers(ReconfigureCallback callback,
                                const Core::Frontend::ControllerParameters& parameters) const override {
        using Core::HID::NpadStyleIndex;
        const std::size_t min_players =
            parameters.enable_single_mode ? 1 : std::max<std::size_t>(std::max<int>(parameters.min_players, 1), 1);
        const std::size_t max_players =
            parameters.enable_single_mode ? 1 : std::max<std::size_t>(std::max<int>(parameters.max_players, 1), min_players);
        const std::size_t pads = std::popcount(pad.ConnectedPlayers() | 1u);
        const std::size_t players = std::clamp(pads, min_players, max_players);
        hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld)->Disconnect();
        for (std::size_t index = 0; index < Core::HID::HIDCore::available_controllers - 2; ++index) {
            auto* controller = hid_core.GetEmulatedControllerByIndex(index);
            controller->Disconnect();
            if (index >= players) continue;
            if (parameters.allow_pro_controller) {
                controller->SetNpadStyleIndex(NpadStyleIndex::Fullkey);
            } else if (parameters.allow_dual_joycons) {
                controller->SetNpadStyleIndex(NpadStyleIndex::JoyconDual);
            } else if (parameters.allow_left_joycon && parameters.allow_right_joycon) {
                controller->SetNpadStyleIndex(index % 2 == 0 ? NpadStyleIndex::JoyconLeft : NpadStyleIndex::JoyconRight);
            } else if (index == 0 && parameters.allow_handheld && !::Settings::IsDockedMode()) {
                controller->SetNpadStyleIndex(NpadStyleIndex::Handheld);
            } else {
                continue;
            }
            controller->Connect(true);
        }
        callback(true);
    }

private:
    Core::HID::HIDCore& hid_core;
    const Pad& pad;
};
}
