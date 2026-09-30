#pragma once

/*
 * Independently authored interoperability declarations.
 * No vendor SDK header or source code is included; see ../NOTICE.md.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ps5::pad {

inline constexpr std::int32_t kPortTypeStandard = 0;
inline constexpr std::int32_t kPortTypeSpecial = 2;
inline constexpr std::int32_t kPortTypeRemoteControl = 16;
inline constexpr std::size_t kSampleSize = 120;
inline constexpr std::size_t kMaxSamples = 64;

using ButtonMask = std::uint32_t;

inline constexpr ButtonMask kButtonCreate = 0x00000001U;
inline constexpr ButtonMask kButtonL3 = 0x00000002U;
inline constexpr ButtonMask kButtonR3 = 0x00000004U;
inline constexpr ButtonMask kButtonOptions = 0x00000008U;
inline constexpr ButtonMask kButtonUp = 0x00000010U;
inline constexpr ButtonMask kButtonRight = 0x00000020U;
inline constexpr ButtonMask kButtonDown = 0x00000040U;
inline constexpr ButtonMask kButtonLeft = 0x00000080U;
inline constexpr ButtonMask kButtonL2 = 0x00000100U;
inline constexpr ButtonMask kButtonR2 = 0x00000200U;
inline constexpr ButtonMask kButtonL1 = 0x00000400U;
inline constexpr ButtonMask kButtonR1 = 0x00000800U;
inline constexpr ButtonMask kButtonTriangle = 0x00001000U;
inline constexpr ButtonMask kButtonCircle = 0x00002000U;
inline constexpr ButtonMask kButtonCross = 0x00004000U;
inline constexpr ButtonMask kButtonSquare = 0x00008000U;
inline constexpr ButtonMask kButtonTouchPad = 0x00100000U;
inline constexpr ButtonMask kButtonIntercepted = 0x80000000U;

struct Stick {
    std::uint8_t x;
    std::uint8_t y;
};

struct AnalogButtons {
    std::uint8_t l2;
    std::uint8_t r2;
    std::array<std::uint8_t, 2> reserved;
};

struct Quaternion {
    float x;
    float y;
    float z;
    float w;
};

struct Vector3 {
    float x;
    float y;
    float z;
};

struct Touch {
    std::uint16_t x;
    std::uint16_t y;
    std::uint8_t id;
    std::array<std::uint8_t, 3> reserved;
};

struct TouchData {
    std::uint8_t count;
    std::array<std::uint8_t, 7> reserved;
    std::array<Touch, 2> points;
};

struct ExtensionData {
    std::uint32_t unit_id;
    std::uint8_t reserved;
    std::uint8_t length;
    std::array<std::uint8_t, 10> data;
};

struct Data {
    ButtonMask buttons;
    Stick left_stick;
    Stick right_stick;
    AnalogButtons triggers;
    Quaternion orientation;
    Vector3 acceleration;
    Vector3 angular_velocity;
    TouchData touch;
    std::int32_t connected;
    std::uint64_t timestamp_us;
    ExtensionData extension;
    std::uint8_t connected_count;
    std::array<std::uint8_t, 2> reserved;
    std::uint8_t device_unique_data_length;
    std::array<std::uint8_t, 12> device_unique_data;
};

struct TouchpadInformation {
    float pixel_density;
    std::uint16_t resolution_x;
    std::uint16_t resolution_y;
};

struct StickInformation {
    std::uint8_t dead_zone_left;
    std::uint8_t dead_zone_right;
};

enum class DeviceClass : std::int32_t {
    invalid = -1,
    standard = 0,
    guitar = 1,
    drum = 2,
    dj_turntable = 3,
    dance_mat = 4,
    navigation = 5,
    steering_wheel = 6,
    stick = 7,
    flight_stick = 8,
    gun = 9,
};

struct ControllerInformation {
    TouchpadInformation touchpad;
    StickInformation sticks;
    std::uint8_t connection_type;
    std::uint8_t connected_count;
    std::int32_t connected;
    std::int32_t device_class;
    std::array<std::uint8_t, 8> reserved;
};

struct Vibration {
    std::uint8_t large_motor;
    std::uint8_t small_motor;
};

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t reserved;
};

enum class TriggerEffectMode : std::int32_t {
    off = 0,
    feedback = 1,
    weapon = 2,
    vibration = 3,
};

struct TriggerEffectCommand {
    std::int32_t mode;
    std::int32_t reserved;
    std::array<std::uint8_t, 48> data;
};

struct TriggerEffect {
    std::uint8_t trigger_mask;
    std::array<std::uint8_t, 7> reserved;
    TriggerEffectCommand l2;
    TriggerEffectCommand r2;
};

enum class TriggerEffectStatus : std::int32_t {
    intercepted = -1,
    off = 0,
    feedback_standby = 1,
    feedback_active = 2,
    weapon_standby = 3,
    weapon_pulling = 4,
    weapon_fired = 5,
    vibration_standby = 6,
    vibration_active = 7,
};

struct TriggerEffectState {
    std::int32_t l2;
    std::int32_t r2;
};

inline constexpr std::uint8_t kTriggerMaskL2 = 1;
inline constexpr std::uint8_t kTriggerMaskR2 = 2;
inline constexpr std::int32_t kVibrationModeAdvanced = 1;
inline constexpr std::int32_t kVibrationModeCompatible = 2;

[[nodiscard]] constexpr Data neutral_data(
    const std::uint64_t timestamp_us = 0) noexcept
{
    Data result{};
    result.left_stick = {128, 128};
    result.right_stick = {128, 128};
    result.orientation.w = 1.0F;
    result.timestamp_us = timestamp_us;
    return result;
}

[[nodiscard]] constexpr bool is_usable(const Data& sample) noexcept
{
    return sample.connected != 0 &&
           (sample.buttons & kButtonIntercepted) == 0;
}

static_assert(sizeof(Data) == kSampleSize);
static_assert(offsetof(Data, touch) == 0x34);
static_assert(offsetof(Data, connected) == 0x4c);
static_assert(offsetof(Data, timestamp_us) == 0x50);
static_assert(sizeof(ControllerInformation) == 28);
static_assert(sizeof(TriggerEffectCommand) == 56);
static_assert(sizeof(TriggerEffect) == 120);
static_assert(std::is_standard_layout_v<Data>);
static_assert(std::is_trivially_copyable_v<Data>);

}  // namespace ps5::pad

extern "C" {

std::int32_t sceUserServiceInitialize(void* parameters);
std::int32_t sceUserServiceGetInitialUser(std::int32_t* user_id);
std::int32_t sceUserServiceTerminate();

std::int32_t scePadInit();
std::int32_t scePadOpen(std::int32_t user_id, std::int32_t port_type,
                        std::int32_t index, const void* parameters);
std::int32_t scePadGetHandle(std::int32_t user_id, std::int32_t port_type,
                             std::int32_t index);
std::int32_t scePadClose(std::int32_t handle);
std::int32_t scePadReadState(std::int32_t handle, ps5::pad::Data* data);
std::int32_t scePadRead(std::int32_t handle, ps5::pad::Data* data,
                        std::int32_t capacity);
std::int32_t scePadGetControllerInformation(
    std::int32_t handle, ps5::pad::ControllerInformation* information);
std::int32_t scePadSetMotionSensorState(std::int32_t handle, bool enabled);
std::int32_t scePadSetTiltCorrectionState(std::int32_t handle, bool enabled);
std::int32_t scePadSetAngularVelocityDeadbandState(std::int32_t handle,
                                                   bool enabled);
std::int32_t scePadResetOrientation(std::int32_t handle);
std::int32_t scePadSetVibration(std::int32_t handle,
                                const ps5::pad::Vibration* vibration);
std::int32_t scePadSetVibrationMode(std::int32_t handle, std::int32_t mode);
std::int32_t scePadSetLightBar(std::int32_t handle,
                               const ps5::pad::Color* color);
std::int32_t scePadResetLightBar(std::int32_t handle);
std::int32_t scePadSetTriggerEffect(
    std::int32_t handle, const ps5::pad::TriggerEffect* effect);
std::int32_t scePadGetTriggerEffectState(
    std::int32_t handle, ps5::pad::TriggerEffectState* state);

}  // extern "C"
