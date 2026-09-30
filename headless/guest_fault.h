// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A guest thread that jumps to an unmapped address takes a prefetch abort, which Eden answers
// by parking that thread for a debugger; without one the game sits on a black screen. The
// first such fault of a session is recorded here so the frontend can end the session like a
// GPU failure (and retry a fault early in the boot).
#include <mutex>
#include <string>
#include <utility>

namespace Eden {
inline std::mutex guest_fault_mutex;
inline std::string guest_fault;
inline void RecordGuestFault(std::string description) {
    std::lock_guard lock(guest_fault_mutex);
    if (guest_fault.empty()) guest_fault = std::move(description);
}
inline std::string TakeGuestFault() {
    std::lock_guard lock(guest_fault_mutex);
    return std::exchange(guest_fault, {});
}
}
