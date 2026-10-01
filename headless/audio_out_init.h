// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mutex>

extern "C" int sceAudioOutInit(void);

namespace Eden {
// AudioOut is initialised once per process: the launcher's sounds and each game's audio both need
// it, and a second sceAudioOutInit reports "already initialised" as an error.
inline bool AudioOutReady() {
    static std::once_flag once;
    static int result = -1;
    std::call_once(once, [] { result = sceAudioOutInit(); });
    return result >= 0 || result == static_cast<int>(0x8026000e);
}
} // namespace Eden
