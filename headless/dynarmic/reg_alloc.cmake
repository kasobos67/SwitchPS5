# ProsperoEden: register-allocator bookkeeping in the x64 JIT emitters (compile time only).
# Upstream releases all 102 host locations after every IR instruction (EndOfAllocScope) and scans
# all of them for every value lookup (ValueLocation): together about a quarter of block compile
# time on the console. The allocator now records the locations changed since the last scope end
# (through the non-const LocInfo) and those that held values at that point, and visits only those.
# Releasing an unchanged location changes nothing (it was released at the scope end after its last
# change, and ReleaseAll is idempotent), and lookups keep upstream's ascending order, so every
# allocation decision and the emitted code stay the same. Host builds check both claims at every
# step (EDEN_REGALLOC_VERIFY).

function(eden_reg_alloc_require source anchor what)
    string(FIND "${source}" "${anchor}" at)
    if(at LESS 0)
        message(FATAL_ERROR "Pinned register allocator changed (${what})")
    endif()
endfunction()

macro(eden_reg_alloc_sources)
    set(eden_ra_dir "${PROJECT_SOURCE_DIR}/src/dynarmic/src/dynarmic/backend/x64")
    file(READ "${eden_ra_dir}/reg_alloc.h" eden_ra_header)
    set(eden_ra_includes [==[#include <array>
]==])
    set(eden_ra_scope [==[    inline void EndOfAllocScope() noexcept {
        for (auto& iter : hostloc_info)
            iter.ReleaseAll();
    }
]==])
    set(eden_ra_locinfo [==[    inline HostLocInfo& LocInfo(const HostLoc loc) noexcept {
        DEBUG_ASSERT(loc != HostLoc::RSP && loc != ABI_JIT_PTR);
        return hostloc_info[size_t(loc)];
    }
]==])
    set(eden_ra_members [==[    size_t reserved_stack_space = 0;
};
]==])
    foreach(anchor eden_ra_includes eden_ra_scope eden_ra_locinfo eden_ra_members)
        eden_reg_alloc_require("${eden_ra_header}" "${${anchor}}" "reg_alloc.h ${anchor}")
    endforeach()
    string(REPLACE "${eden_ra_includes}" [==[#include <array>
#include <bit>
#include <utility>
]==] eden_ra_header "${eden_ra_header}")
    string(REPLACE "${eden_ra_scope}" [==[    inline void EndOfAllocScope() noexcept {
        // ProsperoEden: only the locations changed since the last scope end (reg_alloc.cmake).
#ifdef EDEN_REGALLOC_VERIFY
        EdenVerifyReleased();
#endif
        for (size_t word = 0; word < eden_touched.size(); ++word) {
            for (u64 bits = std::exchange(eden_touched[word], 0); bits != 0; bits &= bits - 1) {
                const size_t i = word * 64 + size_t(std::countr_zero(bits));
                hostloc_info[i].ReleaseAll();
                const u64 bit = u64(1) << (i % 64);
                eden_live[word] = hostloc_info[i].IsEmpty() ? (eden_live[word] & ~bit) : (eden_live[word] | bit);
            }
        }
    }
]==] eden_ra_header "${eden_ra_header}")
    string(REPLACE "${eden_ra_locinfo}" [==[    inline HostLocInfo& LocInfo(const HostLoc loc) noexcept {
        DEBUG_ASSERT(loc != HostLoc::RSP && loc != ABI_JIT_PTR);
        eden_touched[size_t(loc) / 64] |= u64(1) << (size_t(loc) % 64);
        return hostloc_info[size_t(loc)];
    }
]==] eden_ra_header "${eden_ra_header}")
    string(REPLACE "${eden_ra_members}" [==[    size_t reserved_stack_space = 0;
    // ProsperoEden: locations changed since the last EndOfAllocScope, and locations that held
    // values at that point; no other location can hold a value (reg_alloc.cmake).
    static constexpr size_t eden_location_words = (NonSpillHostLocCount + SpillCount + 63) / 64;
    std::array<u64, eden_location_words> eden_touched{};
    std::array<u64, eden_location_words> eden_live{};
#ifdef EDEN_REGALLOC_VERIFY
    // Every location outside the changed set is already in its released state, so upstream's
    // ReleaseAll would leave it unchanged, and its live bit matches its values.
    void EdenVerifyReleased() const noexcept {
        for (size_t i = 0; i < hostloc_info.size(); ++i) {
            if ((eden_touched[i / 64] >> (i % 64)) & 1)
                continue;
            const HostLocInfo& info = hostloc_info[i];
            ASSERT(info.current_references == 0 && info.is_being_used_count == 0 && !info.is_scratch && !info.is_set_last_use);
            ASSERT(info.total_uses != info.accumulated_uses || (info.values.empty() && info.total_uses == 0 && info.max_bit_width == 0));
            ASSERT(bool((eden_live[i / 64] >> (i % 64)) & 1) == !info.values.empty());
        }
    }
#endif
};
]==] eden_ra_header "${eden_ra_header}")
    write_derived("${PORT_BUILD_DIR}/include/dynarmic/backend/x64/reg_alloc.h" "${eden_ra_header}")

    file(READ "${eden_ra_dir}/reg_alloc.cpp" eden_ra_source)
    set(eden_ra_lookup [==[std::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const noexcept {
    for (size_t i = 0; i < hostloc_info.size(); i++)
        if (hostloc_info[i].ContainsValue(value)) {
            //for (size_t j = 0; j < hostloc_info.size(); ++j)
            //    ASSERT((i == j || !hostloc_info[j].ContainsValue(value)) && "duplicate defs");
            return HostLoc(i);
        }
    return std::nullopt;
}
]==])
    eden_reg_alloc_require("${eden_ra_source}" "${eden_ra_lookup}" "reg_alloc.cpp ValueLocation")
    string(REPLACE "${eden_ra_lookup}" [==[std::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const noexcept {
    // ProsperoEden: only locations that held values at the last scope end or changed since (no
    // other one can hold a value), in upstream's ascending order (reg_alloc.cmake).
    std::optional<HostLoc> found;
    for (size_t word = 0; word < eden_live.size() && !found; ++word) {
        for (u64 bits = eden_live[word] | eden_touched[word]; bits != 0; bits &= bits - 1) {
            const size_t i = word * 64 + size_t(std::countr_zero(bits));
            if (hostloc_info[i].ContainsValue(value)) {
                found = HostLoc(i);
                break;
            }
        }
    }
#ifdef EDEN_REGALLOC_VERIFY
    std::optional<HostLoc> full;
    for (size_t i = 0; i < hostloc_info.size() && !full; i++)
        if (hostloc_info[i].ContainsValue(value))
            full = HostLoc(i);
    ASSERT(full == found);
#endif
    return found;
}
]==] eden_ra_source "${eden_ra_source}")
    write_derived("${PORT_BUILD_DIR}/reg_alloc.cpp" "${eden_ra_source}")
    get_target_property(eden_ra_sources dynarmic SOURCES)
    list(FILTER eden_ra_sources EXCLUDE REGEX "(^|/)backend/x64/reg_alloc\\.cpp$")
    set_property(TARGET dynarmic PROPERTY SOURCES "${eden_ra_sources}")
    target_sources(dynarmic PRIVATE "${PORT_BUILD_DIR}/reg_alloc.cpp")
    # The override header shadows upstream's only by include order: a definition on the target
    # recompiles every dynarmic object that saw the upstream layout.
    target_compile_definitions(dynarmic PRIVATE EDEN_REGALLOC_TRACKED=1)
    if(NOT PS5_NATIVE)
        target_compile_definitions(dynarmic PRIVATE EDEN_REGALLOC_VERIFY=1)
    endif()
endmacro()
