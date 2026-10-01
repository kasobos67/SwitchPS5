// SPDX-License-Identifier: GPL-3.0-or-later
// ProsperoEden: compiled A64 blocks shared by the JITs of one guest process.
//
// Every guest core keeps its own JIT (Jit::Impl), emitter and code region, and compiles into its
// own region in parallel with the other cores; the blocks are published here by location and any
// core runs any core's blocks. Generated code therefore reads everything that belongs to one core
// (callbacks, TPIDR registers, exclusive-monitor slots, fast dispatch table, JIT object) through
// the JIT state register instead of embedding it (a64_jitstate.h eden_* fields, callback.cpp).
//
// A direct branch to another block is a site whose 32-bit displacement (or, for return stack
// pushes, 64-bit immediate) is naturally aligned: linking it when the destination is compiled
// and unlinking it when the destination is invalidated are single atomic stores through the
// region's writable view, safe while other cores execute the site. An unlinked site targets a
// stub that stores the destination PC and returns to the dispatcher.
//
// All members are guarded by `mutex` except the clear protocol's atomics (see a64_interface.cpp).
//
// Precompilation: `history` lists every location published, in order and across clears: the block
// list a later session compiles ahead of the game (headless/jit_list.h). A host thread that is no
// guest core compiles such a block into a member's region through `precompile` (jit_impl.inc,
// EdenPrecompile); `precompile_mutex` keeps the members alive while it does.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/icl/interval_set.hpp>

#include "common/common_types.h"
#include "common/container/unordered_map.h"
#include "dynarmic/backend/block_range_information.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend::X64 {

// Guards a group's maps. Critical sections are short (a lookup, or registering one block) and
// the guest cores run on dedicated host CPUs, so waiters spin instead of sleeping in the kernel.
class JitGroupLock {
public:
    void lock() noexcept {
        for (unsigned spins = 0;;) {
            if (!locked.exchange(true, std::memory_order_acquire))
                return;
            while (locked.load(std::memory_order_relaxed)) {
                if (++spins < 4096)
                    __builtin_ia32_pause();
                else
                    std::this_thread::yield();
            }
        }
    }
    bool try_lock() noexcept {
        return !locked.load(std::memory_order_relaxed) && !locked.exchange(true, std::memory_order_acquire);
    }
    void unlock() noexcept {
        locked.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> locked{false};
};

class JitGroup {
public:
    struct Block {
        const void* entrypoint;
        std::size_t size;
    };
    enum class SiteKind : u8 {
        Rel32At2,  // jz/jg rel32: 0F 8x rel32
        Rel32At1,  // jmp rel32: E9 rel32
        Imm64At2,  // mov rcx, imm64: 48 B9 imm64
    };
    struct Site {
        const u8* code;      // executable address of the instruction
        u8* write;           // the same bytes in the region's writable view
        const u8* unlinked;  // destination while the target block is not compiled
        SiteKind kind;
    };
    // A site emitted (unlinked) in a block that is not registered yet, with its target.
    struct PendingSite {
        IR::LocationDescriptor target;
        Site site;
    };

    JitGroupLock mutex;
    // Links left unlinked because the target was out of rel32 reach (development report).
    static inline std::atomic<unsigned long long> far_links{0};

    const Block* Find(IR::LocationDescriptor location) const {
        const auto it = blocks.find(location);
        return it == blocks.end() ? nullptr : &it->second;
    }

    // Points a site at `target`, or at its unlinked destination when `target` is null or out of
    // rel32 reach.
    static void Link(const Site& site, const void* target) {
        const u8* destination = static_cast<const u8*>(target);
        switch (site.kind) {
        case SiteKind::Rel32At2:
        case SiteKind::Rel32At1: {
            const std::size_t opcode = site.kind == SiteKind::Rel32At2 ? 2 : 1;
            const auto displacement = [&](const u8* to) {
                return static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(to)) -
                       static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site.code + opcode + 4));
            };
            std::int64_t value = destination ? displacement(destination) : 0;
            if (destination && value != static_cast<std::int32_t>(value))
                far_links.fetch_add(1, std::memory_order_relaxed);
            if (!destination || value != static_cast<std::int32_t>(value))
                value = displacement(site.unlinked);
            __atomic_store_n(reinterpret_cast<std::uint32_t*>(site.write + opcode),
                             static_cast<std::uint32_t>(static_cast<std::int32_t>(value)), __ATOMIC_RELEASE);
            break;
        }
        case SiteKind::Imm64At2:
            __atomic_store_n(reinterpret_cast<std::uint64_t*>(site.write + 2),
                             reinterpret_cast<std::uint64_t>(destination ? destination : site.unlinked),
                             __ATOMIC_RELEASE);
            break;
        }
    }

    // Publishes a block compiled for `location` covering guest bytes [first, last]: links its own
    // sites to the blocks already compiled, links every site waiting for it, and returns true.
    // Returns false and leaves `result` at the block another core published first (the new
    // block is then never entered, so its sites are dropped).
    bool Register(IR::LocationDescriptor location, Block block, u64 first, u64 last,
                  const std::vector<PendingSite>& own_sites, Block& result) {
        const auto [it, inserted] = blocks.try_emplace(location, block);
        result = it->second;
        if (!inserted)
            return false;
        if (history.size() < history_limit)
            history.push_back(location.Value());
        ranges.AddRange(boost::icl::discrete_interval<u64>::closed(first, last), location);
        for (const PendingSite& pending : own_sites) {
            if (const auto target = blocks.find(pending.target); target != blocks.end())
                Link(pending.site, target->second.entrypoint);
            sites[pending.target].push_back(pending.site);
        }
        if (const auto waiting = sites.find(location); waiting != sites.end())
            for (const Site& site : waiting->second)
                Link(site, block.entrypoint);
        return true;
    }

    // Forgets the blocks overlapping `invalid`, unlinks every site that targets them and returns
    // their locations. Their code stays in place (a core may still be executing it) until the
    // next full clear.
    std::vector<u64> Invalidate(const boost::icl::interval_set<u64>& invalid) {
        std::vector<u64> invalidated;
        for (const auto& location : ranges.InvalidateRanges(invalid)) {
            if (blocks.erase(location) == 0)
                continue;
            invalidated.push_back(location.Value());
            if (const auto waiting = sites.find(location); waiting != sites.end())
                for (const Site& site : waiting->second)
                    Link(site, nullptr);
        }
        return invalidated;
    }

    // With every member quiescent: forget all blocks and sites (their regions are reset too).
    void Clear() {
        blocks.clear();
        sites.clear();
        ranges.ClearCache();
    }

    // Precompilation (see the top of this file). The functions take a member. From `precompile`,
    // -2 means its region is at the reserve kept for the core's own compilations and -3 that the
    // core is compiling there now (it never waits behind a precompilation that could wait).
    static constexpr std::size_t history_limit = 4'000'000;
    std::vector<u64> history;
    int (*precompile)(void* member, u64 location) = nullptr;
    std::size_t (*space)(void* member) = nullptr;
    std::mutex precompile_mutex;

    // Members (Jit::Impl, type-erased) and the clear protocol.
    std::vector<void*> members;
    std::atomic<bool> clearing{false};
    std::atomic<u64> epoch{0};
    std::mutex park_mutex;
    std::condition_variable parked;
    const void* key = nullptr;

private:
    ::Common::unordered_map<IR::LocationDescriptor, Block> blocks;
    ::Common::unordered_map<IR::LocationDescriptor, std::vector<Site>> sites;
    BlockRangeInformation<u64> ranges;
};

}  // namespace Dynarmic::Backend::X64
