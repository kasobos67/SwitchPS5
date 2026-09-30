// SPDX-License-Identifier: GPL-3.0-or-later
#include "devices.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "common/input.h"
#include "common/logging.h"
#include "input_common/input_poller.h"

namespace {
// Signed-in users; unused entries are -1.
struct LoginUserIdList {
    std::int32_t user_id[4];
};
}
extern "C" int sceUserServiceGetForegroundUser(int* user);
extern "C" int sceUserServiceGetLoginUserIdList(LoginUserIdList* list);

namespace Eden {
// Look for newly signed-in (or signed-out) users about once a second at the 4 ms poll interval.
constexpr unsigned kPollsPerScan = 250;

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
    if (slots[0].handle >= 0) return true;
    owns_user_service = sceUserServiceInitialize(nullptr) == 0;
    int user = -1;
    // Player 1 is the application user, as for the title-aware launch controller.
    if (sceUserServiceGetForegroundUser(&user) < 0 || user < 0 || scePadInit() < 0) {
        Close();
        return false;
    }
    OpenSlot(0, user);
    if (slots[0].handle < 0) { Close(); return false; }
    int initial_user = -1;
    const int initial_result = sceUserServiceGetInitialUser(&initial_user);
    LOG_INFO(Input, "EDEN_PAD_USERS foreground={} initial={} initial_rc={}", user, initial_user, initial_result);
    Rescan();
    return true;
}
void Pad::OpenSlot(std::size_t player, int user) {
    int handle = scePadOpen(user, ps5::pad::kPortTypeStandard, 0, nullptr);
    // A handle the process already holds for this user (e.g. from the launcher) is reused.
    if (handle < 0) handle = scePadGetHandle(user, ps5::pad::kPortTypeStandard, 0);
    if (handle < 0) {
        LOG_WARNING(Input, "EDEN_PAD_OPEN_FAILED player={} user={} result={:#x}", player + 1, user,
                    static_cast<unsigned>(handle));
        return;
    }
    slots[player] = {user, handle, 0};
    connected_players |= 1u << player;
    connection_changes |= 1u << player;
    LOG_INFO(Input, "EDEN_PAD_OPEN player={} user={} handle={}", player + 1, user, handle);
}
void Pad::CloseSlot(std::size_t player) {
    auto& slot = slots[player];
    if (slot.handle < 0) return;
    const auto neutral = ps5::pad::neutral_data();
    Consume(player, {&neutral, 1});
    const int closed = scePadClose(slot.handle);
    LOG_INFO(Input, "EDEN_PAD_CLOSE player={} user={} close={}", player + 1, slot.user, closed);
    slot = {};
    connected_players &= ~(1u << player);
    connection_changes |= 1u << player;
}
void Pad::Rescan() {
    LoginUserIdList list;
    std::fill(std::begin(list.user_id), std::end(list.user_id), -1);
    if (sceUserServiceGetLoginUserIdList(&list) < 0) return;
    const auto signed_in = [&](int user) {
        return std::find(std::begin(list.user_id), std::end(list.user_id), user) != std::end(list.user_id);
    };
    // Player 1 stays with the launching user; other players leave when their user signs out.
    for (std::size_t player = 1; player < kMaxPlayers; ++player) {
        if (slots[player].handle >= 0 && !signed_in(slots[player].user)) CloseSlot(player);
    }
    for (const int user : list.user_id) {
        if (user < 0 || std::any_of(slots.begin(), slots.end(), [user](const Slot& slot) {
                return slot.handle >= 0 && slot.user == user; }))
            continue;
        for (std::size_t player = 1; player < kMaxPlayers; ++player) {
            if (slots[player].handle < 0) {
                OpenSlot(player, user);
                break;
            }
        }
    }
}
void Pad::Close() {
    engine->ResetControllers();
    for (std::size_t player = 0; player < kMaxPlayers; ++player) {
        if (slots[player].handle < 0) continue;
        const int closed = scePadClose(slots[player].handle);
        if (player == 0) {
            LOG_INFO(Input, "EDEN_PAD_CLOSED polls={} samples={} usable={} intercepted={} circle={} errors={} last={} close={}",
                     polls, samples_read, usable_samples, intercepted_samples, circle_samples, read_errors, last_result, closed);
        }
        slots[player] = {};
    }
    connected_players = 0;
    connection_changes = 0;
    if (owns_user_service) { sceUserServiceTerminate(); owns_user_service = false; }
}
bool Pad::Poll() {
    if (slots[0].handle >= 0 && ++polls_since_scan >= kPollsPerScan) {
        polls_since_scan = 0;
        Rescan();
    }
    ++polls;
    bool primary = false;
    std::array<ps5::pad::Data, ps5::pad::kMaxSamples> samples{};
    for (std::size_t player = 0; player < kMaxPlayers; ++player) {
        const int handle = slots[player].handle;
        if (handle < 0) continue;
        const int count = scePadRead(handle, samples.data(), samples.size());
        if (player == 0) last_result = count;
        if (count < 0 || count > static_cast<int>(samples.size())) {
            ++read_errors;
            const auto neutral = ps5::pad::neutral_data();
            Consume(player, {&neutral, 1});
            continue;
        }
        for (int i = 0; i < count; ++i) {
            ++samples_read;
            usable_samples += ps5::pad::is_usable(samples[i]);
            intercepted_samples += (samples[i].buttons & ps5::pad::kButtonIntercepted) != 0;
            circle_samples += ps5::pad::is_usable(samples[i]) && (samples[i].buttons & ps5::pad::kButtonCircle);
        }
        Consume(player, {samples.data(), static_cast<std::size_t>(count)});
        primary |= player == 0;
    }
    return primary;
}
void Pad::Consume(std::size_t player, std::span<const ps5::pad::Data> samples) {
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
    auto& last_buttons = slots[player].last_buttons;
    for (const auto& raw : samples) {
        auto sample = is_usable(raw) ? raw : neutral_data(raw.timestamp_us);
        // The shortcuts work from every controller.
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
            engine->SetButtonState(player, button, (sample.buttons & mask) != 0);
        // Guest ZL/ZR are digital; normalize the physical analog triggers first.
        const float left = sample.triggers.l2 / 255.0f;
        const float right = sample.triggers.r2 / 255.0f;
        engine->SetButtonState(player, Button::TriggerZL, (sample.buttons & kButtonL2) || left >= trigger_threshold);
        engine->SetButtonState(player, Button::TriggerZR, (sample.buttons & kButtonR2) || right >= trigger_threshold);
        engine->SetStickPosition(player, 0, axis(sample.left_stick.x), -axis(sample.left_stick.y));
        engine->SetStickPosition(player, 1, axis(sample.right_stick.x), -axis(sample.right_stick.y));
    }
}
}
