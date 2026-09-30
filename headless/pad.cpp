// SPDX-License-Identifier: GPL-3.0-or-later
#include "devices.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "common/input.h"
#include "common/logging.h"
#include "input_common/input_poller.h"

extern "C" int sceUserServiceGetForegroundUser(int* user);

namespace Eden {
Pad::Pad(float deadzone_, float threshold)
    : engine{std::make_shared<InputCommon::VirtualGamepad>("virtual_gamepad")},
      deadzone{deadzone_}, trigger_threshold{threshold} {
    if (!std::isfinite(deadzone) || deadzone < 0 || deadzone >= 1 ||
        !std::isfinite(threshold) || threshold <= 0 || threshold > 1)
        throw std::invalid_argument("Invalid pad calibration");
    Common::Input::RegisterInputFactory("virtual_gamepad",
        std::make_shared<InputCommon::InputFactory>(engine));
}
Pad::~Pad() {
    Close();
    Common::Input::UnregisterInputFactory("virtual_gamepad");
}
bool Pad::Open() {
    if (handle >= 0) return true;
    owns_user_service = sceUserServiceInitialize(nullptr) == 0;
    int user = -1;
    // Use the same application user as the title-aware launch controller.
    if (sceUserServiceGetForegroundUser(&user) < 0 || user < 0 || scePadInit() < 0) {
        Close();
        return false;
    }
    handle = scePadOpen(user, ps5::pad::kPortTypeStandard, 0, nullptr);
    if (handle < 0) { Close(); return false; }
    int initial_user = -1;
    const int initial_result = sceUserServiceGetInitialUser(&initial_user);
    LOG_INFO(Input, "EDEN_PAD_OPEN foreground={} initial={} initial_rc={} handle={}",
             user, initial_user, initial_result, handle);
    return true;
}
void Pad::Close() {
    engine->ResetControllers();
    if (handle >= 0) {
        const int closed = scePadClose(handle);
        LOG_INFO(Input, "EDEN_PAD_CLOSED polls={} samples={} usable={} intercepted={} circle={} errors={} last={} close={}",
                 polls, samples_read, usable_samples, intercepted_samples, circle_samples, read_errors, last_result, closed);
        handle = -1;
    }
    if (owns_user_service) { sceUserServiceTerminate(); owns_user_service = false; }
}
bool Pad::Poll() {
    std::array<ps5::pad::Data, ps5::pad::kMaxSamples> samples{};
    const int count = handle < 0 ? -1 : scePadRead(handle, samples.data(), samples.size());
    ++polls; last_result = count;
    if (count < 0 || count > static_cast<int>(samples.size())) {
        ++read_errors;
        const auto neutral = ps5::pad::neutral_data();
        Consume({&neutral, 1});
        return false;
    }
    for (int i = 0; i < count; ++i) {
        ++samples_read;
        usable_samples += ps5::pad::is_usable(samples[i]);
        intercepted_samples += (samples[i].buttons & ps5::pad::kButtonIntercepted) != 0;
        circle_samples += ps5::pad::is_usable(samples[i]) && (samples[i].buttons & ps5::pad::kButtonCircle);
    }
    Consume({samples.data(), static_cast<std::size_t>(count)});
    return true;
}
void Pad::Consume(std::span<const ps5::pad::Data> samples) {
    using namespace ps5::pad;
    using Button = InputCommon::VirtualGamepad::VirtualButton;
    static constexpr std::pair<ButtonMask, Button> buttons[] = {
        {kButtonCircle, Button::ButtonA}, {kButtonCross, Button::ButtonB},
        {kButtonTriangle, Button::ButtonX}, {kButtonSquare, Button::ButtonY},
        {kButtonL3, Button::StickL}, {kButtonR3, Button::StickR},
        {kButtonL1, Button::TriggerL}, {kButtonR1, Button::TriggerR},
        {kButtonOptions, Button::ButtonPlus}, {kButtonCreate, Button::ButtonMinus},
        {kButtonLeft, Button::ButtonLeft}, {kButtonUp, Button::ButtonUp},
        {kButtonRight, Button::ButtonRight}, {kButtonDown, Button::ButtonDown},
        {kButtonTouchPad, Button::ButtonCapture},
    };
    const auto axis = [this](u8 raw) {
        const float x = (static_cast<int>(raw) - 128) / (raw < 128 ? 128.0f : 127.0f);
        return std::abs(x) <= deadzone ? 0.0f :
            std::copysign((std::abs(x) - deadzone) / (1.0f - deadzone), x);
    };
    for (const auto& raw : samples) {
        auto sample = is_usable(raw) ? raw : neutral_data(raw.timestamp_us);
        constexpr ButtonMask menu_chord = kButtonTouchPad | kButtonL1;
        constexpr ButtonMask hud_chord = kButtonTouchPad | kButtonR1;
        const auto pressed = sample.buttons;
        if ((pressed & menu_chord) == menu_chord &&
            (last_buttons & menu_chord) != menu_chord)
            return_to_menu = true;
        if ((pressed & hud_chord) == hud_chord &&
            (last_buttons & hud_chord) != hud_chord)
            hud_toggle = true;
        if ((pressed & menu_chord) == menu_chord || (pressed & hud_chord) == hud_chord)
            sample.buttons &= ~(kButtonTouchPad | kButtonL1 | kButtonR1);
        last_buttons = pressed;
        for (const auto [mask, button] : buttons)
            engine->SetButtonState(0, button, (sample.buttons & mask) != 0);
        // Guest ZL/ZR are digital; normalize the physical analog triggers first.
        const float left = sample.triggers.l2 / 255.0f;
        const float right = sample.triggers.r2 / 255.0f;
        engine->SetButtonState(0, Button::TriggerZL, (sample.buttons & kButtonL2) || left >= trigger_threshold);
        engine->SetButtonState(0, Button::TriggerZR, (sample.buttons & kButtonR2) || right >= trigger_threshold);
        engine->SetStickPosition(0, 0, axis(sample.left_stick.x), -axis(sample.left_stick.y));
        engine->SetStickPosition(0, 1, axis(sample.right_stick.x), -axis(sample.right_stick.y));
    }
}
}
