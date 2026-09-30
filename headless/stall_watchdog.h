// SPDX-License-Identifier: GPL-3.0-or-later
// Development builds: where does a stalled game boot stop? While armed (game boot, up to the
// guest's start), 15 s without boot progress makes a watchdog interrupt the main thread and the
// session's key threads with SIGUSR1. The handler copies the program counter and the stack above
// the stack pointer; the watchdog writes the PC and every stack word that points into this
// executable (candidate return addresses) to the kernel log, then stdout (a stall may be stuck
// writing the piped stdout). Symbolize with the run's candidate.elf:
//   address in the ELF = logged address - (anchor - address of Eden::Stall::Signal in the ELF)
#pragma once
#if defined(EDEN_DEV_PROFILE) && defined(PS5_NATIVE)
#include <pthread.h>
#include <signal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>
#include "diagnostics.h"

// Linker-defined: the executable's ELF header (its first byte).
extern "C" char __ehdr_start;

namespace Eden::Stall {
inline constexpr std::size_t kStackWords = 4096;
struct Watched {
    char name[24]{};
    pthread_t thread{};
    std::atomic<bool> ready{false};
};
inline std::array<Watched, 64> watched{};
inline std::atomic<unsigned> watched_count{0};
inline std::atomic<unsigned> progress{0};
inline std::atomic<bool> armed{false};
inline std::atomic<const char*> stage{"none"};
inline pthread_t main_thread{};
inline std::uintptr_t main_stack_top = 0;
// One sample at a time: written by the handler, read by the watchdog once `sampled` is set.
inline std::atomic<std::uintptr_t> sample_stack_top{0};
inline std::atomic<bool> sampled{false};
inline std::uintptr_t sample_pc = 0, sample_sp = 0;
inline std::size_t sample_words = 0;
inline std::uintptr_t sample_stack[kStackWords];

inline void Print(const char* line) {
#if defined(__PROSPERO__)
    (void)sceKernelDebugOutText(0, line);
#endif
    std::fputs(line, stdout);
}

inline void Trace(const char* point) {
    char line[160];
    std::snprintf(line, sizeof(line), "EDEN_BOOT %s\n", point);
    Print(line);
}

// [begin, end) of the executable's code, from the program headers of its mapped ELF header.
inline std::pair<std::uintptr_t, std::uintptr_t> CodeRange() {
    const auto image = reinterpret_cast<std::uintptr_t>(&__ehdr_start);
    const auto* header = reinterpret_cast<const unsigned char*>(image);
    std::uintptr_t begin = ~std::uintptr_t{0}, end = 0;
    if (!std::memcmp(header, "\x7f" "ELF", 4) && header[4] == 2) {
        std::uint64_t phoff;
        std::uint16_t phentsize, phnum;
        std::memcpy(&phoff, header + 32, sizeof(phoff));
        std::memcpy(&phentsize, header + 54, sizeof(phentsize));
        std::memcpy(&phnum, header + 56, sizeof(phnum));
        for (unsigned i = 0; i < phnum && phentsize >= 56; ++i) {
            const unsigned char* program = header + phoff + i * phentsize;
            std::uint32_t type, flags;
            std::uint64_t vaddr, memsz;
            std::memcpy(&type, program, sizeof(type));
            std::memcpy(&flags, program + 4, sizeof(flags));
            std::memcpy(&vaddr, program + 16, sizeof(vaddr));
            std::memcpy(&memsz, program + 40, sizeof(memsz));
            if (type != 1 || !(flags & 1)) continue; // PT_LOAD, PF_X
            begin = std::min<std::uintptr_t>(begin, image + vaddr);
            end = std::max<std::uintptr_t>(end, image + vaddr + memsz);
        }
    }
    if (begin >= end) return {image, image + (std::uintptr_t{256} << 20)};
    return {begin, end};
}

inline void Signal(int, siginfo_t*, void* context) {
    // Firmware 6.02 ucontext offsets, as qualified for the PC sampler (headless/performance.cpp).
    const auto* words = static_cast<const std::uintptr_t*>(context);
    sample_pc = words[224 / sizeof(std::uintptr_t)];
    sample_sp = words[248 / sizeof(std::uintptr_t)];
    std::uintptr_t top = sample_stack_top.load(std::memory_order_acquire);
    // Without a known stack top, stay inside the stack pointer's page.
    if (top <= sample_sp || top - sample_sp > (std::uintptr_t{64} << 20)) top = (sample_sp | 4095) + 1;
    std::size_t count = 0;
    if (sample_sp > 0x10000 && !(sample_sp & 7)) {
        count = std::min<std::size_t>((top - sample_sp) / sizeof(std::uintptr_t), kStackWords);
        const auto* stack = reinterpret_cast<const std::uintptr_t*>(sample_sp);
        for (std::size_t i = 0; i < count; ++i) sample_stack[i] = stack[i];
    }
    sample_words = count;
    sampled.store(true, std::memory_order_release);
}

// Every thread that names itself (the SetCurrentThreadName hook), first thing.
inline void NoteThread(const char* name) {
    if (!std::strcmp(name, "GPU")) Trace("GPU thread named");
    const unsigned index = watched_count.fetch_add(1, std::memory_order_acq_rel);
    if (index >= watched.size()) return;
    auto& entry = watched[index];
    std::strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.thread = pthread_self();
    entry.ready.store(true, std::memory_order_release);
}

inline void Progress(const char* name) {
    stage.store(name, std::memory_order_relaxed);
    progress.fetch_add(1, std::memory_order_relaxed);
}
inline void Tick() { progress.fetch_add(1, std::memory_order_relaxed); }

inline void Sample(pthread_t thread, std::uintptr_t stack_top, const char* name, unsigned round, double seconds) {
    char line[1152];
    sampled.store(false, std::memory_order_relaxed);
    sample_stack_top.store(stack_top, std::memory_order_release);
    if (pthread_kill(thread, SIGUSR1) != 0) {
        std::snprintf(line, sizeof(line), "EDEN_STALL round=%u thread=%s signal_failed\n", round, name);
        Print(line);
        return;
    }
    for (int wait = 0; wait < 100 && !sampled.load(std::memory_order_acquire); ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!sampled.load(std::memory_order_acquire)) {
        std::snprintf(line, sizeof(line), "EDEN_STALL round=%u thread=%s no_response\n", round, name);
        Print(line);
        return;
    }
    const auto [code_begin, code_end] = CodeRange();
    std::snprintf(line, sizeof(line),
                  "EDEN_STALL round=%u thread=%s stage=%s seconds=%.0f pc=%lx sp=%lx anchor=%lx code=%lx-%lx words=%zu\n",
                  round, name, stage.load(std::memory_order_relaxed), seconds,
                  static_cast<unsigned long>(sample_pc), static_cast<unsigned long>(sample_sp),
                  static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&Signal)),
                  static_cast<unsigned long>(code_begin), static_cast<unsigned long>(code_end), sample_words);
    Print(line);
    const auto start = [&] {
        return std::snprintf(line, sizeof(line), "EDEN_STALL_STACK round=%u thread=%s", round, name);
    };
    int length = start();
    unsigned found = 0;
    for (std::size_t i = 0; i < sample_words && found < 64; ++i) {
        const std::uintptr_t word = sample_stack[i];
        if (word < code_begin || word >= code_end) continue;
        if (length > 1000) {
            std::snprintf(line + length, sizeof(line) - length, "\n");
            Print(line);
            length = start();
        }
        length += std::snprintf(line + length, sizeof(line) - length, " %zx:%lx", i, static_cast<unsigned long>(word));
        ++found;
    }
    std::snprintf(line + length, sizeof(line) - length, "\n");
    Print(line);
}

inline bool Interesting(const char* name) {
    for (const char* wanted : {"GPU", "VulkanWorker", "VulkanPresent", "GPUFencingThread",
                               "VkPipelineSerialization", "TimeWorker", "CPUCore_0"})
        if (!std::strcmp(name, wanted)) return true;
    return false;
}

inline void Loop() {
    unsigned last = progress.load(std::memory_order_relaxed);
    auto changed = std::chrono::steady_clock::now();
    unsigned round = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const unsigned now_progress = progress.load(std::memory_order_relaxed);
        const auto now = std::chrono::steady_clock::now();
        if (!armed.load(std::memory_order_acquire) || now_progress != last) {
            last = now_progress;
            changed = now;
            round = 0;
            continue;
        }
        // 15 s without progress, then every 5 s: at most six rounds per stall.
        const double seconds = std::chrono::duration<double>(now - changed).count();
        if (round >= 6 || seconds < 15 + 5.0 * round) continue;
        Sample(main_thread, main_stack_top, "main", round, seconds);
        const unsigned count = std::min<unsigned>(watched_count.load(std::memory_order_acquire), watched.size());
        unsigned builders = 0;
        for (unsigned i = 0; i < count; ++i) {
            auto& entry = watched[i];
            if (!entry.ready.load(std::memory_order_acquire)) continue;
            if (!std::strcmp(entry.name, "VkPipelineBuilder") ? builders++ >= 2 : !Interesting(entry.name)) continue;
            Sample(entry.thread, 0, entry.name, round, seconds);
        }
        ++round;
    }
}

// Main thread, once. `stack_top`: an address in main()'s frame, so the main thread's stack from
// a sampled stack pointer up to there is live.
inline void Start(std::uintptr_t stack_top) {
    struct sigaction previous{}, action{};
    if (sigaction(SIGUSR1, nullptr, &previous) || previous.sa_handler != SIG_DFL) {
        Print("EDEN_STALL unavailable: SIGUSR1 is in use\n");
        return;
    }
    action.sa_sigaction = Signal;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, nullptr)) return;
    main_thread = pthread_self();
    main_stack_top = stack_top;
    std::thread(Loop).detach();
}

// A game boot starts: forget the previous session's threads (they have exited).
inline void Arm() {
    for (auto& entry : watched) entry.ready.store(false, std::memory_order_relaxed);
    watched_count.store(0, std::memory_order_release);
    Progress("arm");
    armed.store(true, std::memory_order_release);
}
inline void Disarm() { armed.store(false, std::memory_order_release); }
}
#endif
