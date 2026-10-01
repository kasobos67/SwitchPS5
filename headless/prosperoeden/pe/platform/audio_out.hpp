// ProsperoEden - Audio output thread through sceAudioOut.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/audio/mixer.hpp"

#include <atomic>
#include <cstdint>
#include <pthread.h>

namespace pe::ps5
{

// Opens the main 48 kHz stereo port (system user, 256-frame grains) and feeds
// it from the mixer on its own thread. sceAudioOutOutput blocks for each
// grain, which paces the thread. The caller initialises AudioOut first.
class AudioOut
{
  public:
    static constexpr int kGrainFrames = 256;

    AudioOut() = default;
    AudioOut(const AudioOut &) = delete;
    AudioOut &operator=(const AudioOut &) = delete;
    ~AudioOut();

    bool start(audio::Mixer &mixer);
    void stop();

    std::uint64_t grains() const
    {
        return grains_.load(std::memory_order_relaxed);
    }
    std::uint64_t errors() const
    {
        return errors_.load(std::memory_order_relaxed);
    }

  private:
    static void *thread_main(void *self);
    void run();

    audio::Mixer *mixer_ = nullptr;
    int handle_ = -1;
    pthread_t thread_{};
    bool running_ = false;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> grains_{0};
    std::atomic<std::uint64_t> errors_{0};
};

} // namespace pe::ps5
