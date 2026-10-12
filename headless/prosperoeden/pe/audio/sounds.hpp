// ProsperoEden - Launcher sound cues and the loaded sound bank.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "pe/audio/mixer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pe::audio
{

// What the launcher asks to hear; the frontend plays the frame's cues.
enum class Cue : std::uint8_t
{
    focus,       // the highlight moved
    select,      // a choice confirmed
    back,        // a screen closed
    page,        // a list jumped a page
    toggle,      // a switch or option changed
    slider,      // a level moved one step
    open,        // a screen opened
    modal_open,  // a dialog rose
    modal_close, // a dialog closed
    error,       // the action is not available
    saved,       // a setting was stored
    launch,      // a game starts
    welcome,     // the launcher appeared for the first time
    resume,      // the launcher appeared again after a game
    notify,      // a message needs attention
    // Original qlaunch context cues. Keep the preceding values stable: existing
    // frame queues and recordings remain compatible, including external callers.
    focus_menu,
    focus_navigation,
    focus_account,
    focus_applet,
    focus_library_button,
    focus_footer,
    home_scroll,
    library_open,
    library_launch,
    launch_effect,
    toggle_off,
    slider_release,
    account_select,
    controllers_open,
    news_open,
    album_open,
    profile_open,
    scroll_limit,
    count,
};
constexpr std::size_t kCueCount = static_cast<std::size_t>(Cue::count);

const char *cue_name(Cue cue);

// Recorded cues: "<name>_NN.wav" (NN from 01) in one folder, 48 kHz PCM. Each
// play takes the next recorded variation at its original pitch. A cue without a
// recording plays a short synthesized fallback tone.
class SoundBank
{
  public:
    // Returns the number of files loaded; errors name the rejected ones.
    int load(const std::string &directory, std::vector<std::string> *errors = nullptr);
    void play(Mixer &mixer, Cue cue);
    bool has_recording(Cue cue) const
    {
        return cue < Cue::count && !sets_[static_cast<std::size_t>(cue)].empty();
    }

  private:
    struct Sound
    {
        std::vector<float> samples;
        Clip clip;
    };
    std::vector<std::unique_ptr<Sound>> sets_[kCueCount];
    std::size_t next_[kCueCount]{};
    std::uint32_t jitter_state_ = 0x9e3779b9u;
};

} // namespace pe::audio
