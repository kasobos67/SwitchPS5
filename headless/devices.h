// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>
#include "audio_core/sink/sink.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "ps5_pad.hpp"

namespace Eden {
class Pad final {
public:
    explicit Pad(float deadzone = 0.08f, float trigger_threshold = 0.5f);
    ~Pad();
    Pad(const Pad&) = delete;
    Pad& operator=(const Pad&) = delete;
    bool Open();
    bool Poll();
    bool TakeReturnToMenu() { return return_to_menu.exchange(false); }
    bool TakeHudToggle() { return hud_toggle.exchange(false); }
    void Close();
    void Consume(std::span<const ps5::pad::Data> samples);
    InputCommon::VirtualGamepad& Engine() { return *engine; }
private:
    std::shared_ptr<InputCommon::VirtualGamepad> engine;
    float deadzone;
    float trigger_threshold;
    int handle = -1;
    bool owns_user_service = false;
    std::atomic<bool> return_to_menu = false;
    std::atomic<bool> hud_toggle = false;
    u32 last_buttons = 0;
    u64 polls = 0, samples_read = 0, usable_samples = 0, intercepted_samples = 0, circle_samples = 0, read_errors = 0;
    int last_result = 0;
};

class AudioStream final : public AudioCore::Sink::SinkStream {
public:
    AudioStream(Core::System&, u32 channels, const std::string&, AudioCore::Sink::StreamType);
    ~AudioStream() override;
    void Start(bool resume = false) override;
    void Stop() override;
    void Finalize() override;
    void AppendBuffer(AudioCore::Sink::SinkBuffer&, std::span<s16>) override;
    bool Healthy() const { return !failed; }
private:
    int handle = -1;
    std::atomic<bool> failed = false;
    bool in_flight = false;
    u64 output_frames = 0;
    u64 nonzero_frames = 0;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::jthread worker;
};

class AudioSink final : public AudioCore::Sink::Sink {
public:
    AudioCore::Sink::SinkStream* AcquireSinkStream(Core::System&, u32, const std::string&,
                                                  AudioCore::Sink::StreamType) override;
    void CloseStream(AudioCore::Sink::SinkStream*) override;
    void CloseStreams() override { streams.clear(); }
    f32 GetDeviceVolume() const override { return device_volume; }
    void SetDeviceVolume(f32) override;
    void SetSystemVolume(f32) override;
private:
    std::vector<AudioCore::Sink::SinkStreamPtr> streams;
    float device_volume = 1.0f;
    float system_volume = 1.0f;
};
}
