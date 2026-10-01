// ProsperoEden - Audio output thread through sceAudioOut.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/platform/audio_out.hpp"

#include "pe/core/log.hpp"

extern "C"
{
    int sceAudioOutOpen(int user, int type, int index, unsigned int grain_frames,
                        unsigned int frequency, unsigned int format);
    int sceAudioOutOutput(int handle, const void *samples);
    int sceAudioOutClose(int handle);
    int sceKernelUsleep(unsigned int microseconds);
}

namespace pe::ps5
{

namespace
{

constexpr int kSystemUser = 0xff;
constexpr int kPortMain = 0;
constexpr unsigned int kFormatS16Stereo = 1;

} // namespace

AudioOut::~AudioOut()
{
    stop();
}

bool AudioOut::start(audio::Mixer &mixer)
{
    handle_ = sceAudioOutOpen(kSystemUser, kPortMain, 0, kGrainFrames, audio::kSampleRate,
                              kFormatS16Stereo);
    if (handle_ < 0)
    {
        sys::log("audio sceAudioOutOpen=0x%08x", static_cast<unsigned>(handle_));
        handle_ = -1;
        return false;
    }
    mixer_ = &mixer;
    stop_.store(false);
    // Never name threads: pthread_setname_np hangs on this platform.
    if (pthread_create(&thread_, nullptr, &AudioOut::thread_main, this) != 0)
    {
        sys::log("audio thread creation failed");
        sceAudioOutClose(handle_);
        handle_ = -1;
        return false;
    }
    running_ = true;
    sys::log("audio open handle=%d grain=%d rate=%d", handle_, kGrainFrames,
             audio::kSampleRate);
    return true;
}

void *AudioOut::thread_main(void *self)
{
    static_cast<AudioOut *>(self)->run();
    return nullptr;
}

void AudioOut::run()
{
    alignas(64) std::int16_t grain[kGrainFrames * 2];
    while (!stop_.load(std::memory_order_relaxed))
    {
        mixer_->render(grain, kGrainFrames);
        if (sceAudioOutOutput(handle_, grain) < 0)
        {
            errors_.fetch_add(1, std::memory_order_relaxed);
            sceKernelUsleep(5000);
            continue;
        }
        grains_.fetch_add(1, std::memory_order_relaxed);
    }
    sceAudioOutOutput(handle_, nullptr); // drain the queued grain
}

void AudioOut::stop()
{
    if (running_)
    {
        stop_.store(true);
        pthread_join(thread_, nullptr);
        running_ = false;
    }
    if (handle_ >= 0)
        sceAudioOutClose(handle_);
    handle_ = -1;
}

} // namespace pe::ps5
