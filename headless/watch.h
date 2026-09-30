// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Development write watch on one guest range, set by dev-settings watch=main+OFFSET:SIZE (hex,
// relative to the main module). The range's pages are marked debug so JIT accesses take the
// memory callbacks, which report writes into the range with the guest thread, value and
// frame-pointer return chain. dump=main+OFFSET:SIZE logs guest code words
// once the game is loaded. watch_snapshot=OFFSET:VALUE (hex) logs every guest thread once, at
// the first watched write of VALUE at OFFSET.
#include <atomic>
#include <string>
#include "common/common_types.h"
#include "common/logging.h"
#include "core/arm/debug.h"
#include "crash_site.h"

namespace Eden::Watch {

struct Range {
    u64 offset = 0;
    u64 size = 0;
};
inline Range watch_range;
inline Range dump_range;
inline std::atomic<u64> begin{0};
inline std::atomic<u64> end{0};
inline std::atomic<unsigned> reports{0};
inline Range snapshot{~u64{0}, 0};  // offset, value
inline std::atomic<bool> snapshot_taken{false};

// "watch=main+5F0F5C0:48", "dump=main+1A5780:100" or "watch_snapshot=40:1".
inline bool Parse(const std::string& entry) {
    if (entry.starts_with("watch_snapshot=")) {
        const auto colon = entry.find(':');
        if (colon == std::string::npos) return false;
        snapshot.offset = std::stoull(entry.substr(15, colon - 15), nullptr, 16);
        snapshot.size = std::stoull(entry.substr(colon + 1), nullptr, 16);
        return true;
    }
    Range* range = entry.starts_with("watch=main+") ? &watch_range :
                   entry.starts_with("dump=main+") ? &dump_range : nullptr;
    if (!range) return false;
    const auto plus = entry.find('+');
    const auto colon = entry.find(':', plus);
    if (colon == std::string::npos) return false;
    range->offset = std::stoull(entry.substr(plus + 1, colon - plus - 1), nullptr, 16);
    range->size = std::stoull(entry.substr(colon + 1), nullptr, 16);
    return range->size != 0;
}

inline bool Hit(u64 vaddr, unsigned size) {
    const u64 first = begin.load(std::memory_order_relaxed);
    return first != 0 && vaddr + size > first && vaddr < end.load(std::memory_order_relaxed);
}

template <typename Memory, typename Jit, typename Thread>
void Report(u64 vaddr, unsigned size, u64 value, Memory& memory, const Jit& jit, const Thread& thread,
            std::size_t core) {
    if (reports.fetch_add(1, std::memory_order_relaxed) >= 400) return;
    // Registers are the JIT state at the callback (the current block may not have stored
    // its own updates yet), so the chain can start one frame out.
    std::string chain;
    u64 fp = jit.GetRegister(29);
    for (int i = 0; i < 8 && fp && memory.IsValidVirtualAddressRange(fp, 16); ++i) {
        chain += fmt::format(" {:X}", memory.Read64(fp + 8));
        fp = memory.Read64(fp);
    }
    LOG_CRITICAL(Core_ARM, "EDEN_WATCH_WRITE core={} thread={} name={} pc={:016X} lr={:016X} offset={:#x} size={} value={:016X} chain={}",
                 core, thread.GetThreadId(), Core::GetThreadName(&thread).value_or("-"), jit.GetPC(),
                 jit.GetRegister(30), vaddr - begin.load(std::memory_order_relaxed), size, value, chain);
    if (vaddr - begin.load(std::memory_order_relaxed) == snapshot.offset && value == snapshot.size &&
        !snapshot_taken.exchange(true))
        ::Eden::CrashSite::DumpThreads("watch", *thread.GetOwnerProcess());
}

} // namespace Eden::Watch
