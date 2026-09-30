// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Development dump for a guest call through a null or unmapped pointer: the guest code
// around each return address of the frame-pointer chain, the words at the argument and
// callee-saved registers, and every guest thread of the process (name, state, wait reason,
// core, priority, saved-context backtrace), so the faulting call and what the other threads
// were doing can be read offline.
#include <string>
#include "common/common_types.h"
#include "common/logging.h"
#include "core/arm/debug.h"

namespace Eden::CrashSite {

template <typename Memory, typename Jit>
void Dump(Memory& memory, const Jit& jit) {
    const auto code = [&](const char* what, u64 at) {
        std::string line = fmt::format("EDEN_CRASH_CODE {} ret={:016X} from={:016X}:", what, at, at - 0x100);
        for (u64 a = at - 0x100; a < at + 0x40; a += 4)
            line += fmt::format(" {:08X}", memory.IsValidVirtualAddressRange(a, 4) ? memory.Read32(a) : 0u);
        LOG_CRITICAL(Core_ARM, "{}", line);
    };
    code("lr", jit.GetRegister(30));
    u64 fp = jit.GetRegister(29);
    for (int i = 0; i < 6 && fp && memory.IsValidVirtualAddressRange(fp, 16); ++i) {
        const u64 ret = memory.Read64(fp + 8);
        fp = memory.Read64(fp);
        code("frame", ret);
    }
    for (const int r : {0, 1, 2, 3, 8, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28}) {
        const u64 at = jit.GetRegister(r);
        std::string line = fmt::format("EDEN_CRASH_DATA x{}={:016X}:", r, at);
        for (u64 a = at; a < at + 0x80; a += 8)
            line += fmt::format(" {:016X}", memory.IsValidVirtualAddressRange(a, 8) ? memory.Read64(a) : u64{0});
        LOG_CRITICAL(Core_ARM, "{}", line);
    }
}

// Saved contexts: a thread running on another core shows where it last switched out.
template <typename Process>
void DumpThreads(const char* reason, Process& process) {
    for (const auto& thread : process.GetThreadList()) {
        std::string frames;
        for (const auto& entry : Core::GetBacktrace(&thread))
            frames += fmt::format(" {}+{:X}", entry.module, entry.offset);
        LOG_CRITICAL(Core_ARM, "EDEN_CRASH_THREAD {} id={} name={} state={} wait={} core={} prio={} frames={}",
                     reason, thread.GetThreadId(), Core::GetThreadName(&thread).value_or("-"),
                     Core::GetThreadState(&thread), Core::GetThreadWaitReason(&thread),
                     thread.GetActiveCore(), thread.GetPriority(), frames);
    }
}

} // namespace Eden::CrashSite
