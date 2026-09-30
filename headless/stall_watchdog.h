// SPDX-License-Identifier: GPL-3.0-or-later
// Development builds: where does a stalled game boot stop? The boot writes trace points to the
// kernel log (EDEN_BOOT), and while armed (game boot, up to the guest's start) a watchdog reports
// every 5 s once the boot has made no progress for 15 s (EDEN_STALL): the last stage and the heap
// arena creation lock's holder and waiters (headless/heap_arenas.inc). Kernel log only: stdout is
// block-buffered, and signals sent to threads during a boot hung or crashed it.
#pragma once
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include "diagnostics.h"

extern "C" unsigned long eden_heap_create_lock_state(unsigned* waiters);

namespace Eden::Stall {
inline std::atomic<unsigned> progress{0};
inline std::atomic<bool> armed{false};
inline std::atomic<const char*> stage{"none"};

inline void Print(const char* line) {
#if defined(__PROSPERO__)
    (void)sceKernelDebugOutText(0, line);
#else
    std::fputs(line, stderr);
#endif
}

inline void Trace(const char* point) {
    char line[160];
    std::snprintf(line, sizeof(line), "EDEN_BOOT %s\n", point);
    Print(line);
}

// Every thread that names itself (the SetCurrentThreadName hook), first thing.
inline void NoteThread(const char* name) {
    if (!std::strcmp(name, "GPU")) Trace("GPU thread named");
}

inline void Progress(const char* name) {
    stage.store(name, std::memory_order_relaxed);
    progress.fetch_add(1, std::memory_order_relaxed);
}
inline void Tick() { progress.fetch_add(1, std::memory_order_relaxed); }

inline void Loop() {
    unsigned last = progress.load(std::memory_order_relaxed);
    auto changed = std::chrono::steady_clock::now();
    unsigned reports = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const unsigned now_progress = progress.load(std::memory_order_relaxed);
        const auto now = std::chrono::steady_clock::now();
        if (!armed.load(std::memory_order_acquire) || now_progress != last) {
            last = now_progress;
            changed = now;
            reports = 0;
            continue;
        }
        const double seconds = std::chrono::duration<double>(now - changed).count();
        if (reports >= 12 || seconds < 15 + 5.0 * reports) continue;
        unsigned waiters = 0;
        const unsigned long holder = eden_heap_create_lock_state(&waiters);
        char line[256];
        std::snprintf(line, sizeof(line), "EDEN_STALL stage=%s seconds=%.0f heap_create_holder=%lx heap_create_waiters=%u\n",
                      stage.load(std::memory_order_relaxed), seconds, holder, waiters);
        Print(line);
        ++reports;
    }
}

// Main thread, once.
inline void Start() { std::thread(Loop).detach(); }

// A game boot starts / its guest runs.
inline void Arm() {
    Progress("arm");
    armed.store(true, std::memory_order_release);
}
inline void Disarm() { armed.store(false, std::memory_order_release); }
}
#endif
