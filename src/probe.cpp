// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <xbyak/xbyak.h>
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/exclusive_monitor.h"
#include "common/fiber.h"
#include "common/scope_exit.h"
#include "common/thread.h"
#include "core/hardware_properties.h"
#include "common/host_memory.h"
#include "common/page_table.h"
#include "guest.hpp"

namespace {
using namespace Dynarmic;
constexpr std::uint64_t code_base = 0x1000, data_base = 0x100000;
constexpr std::size_t data_size = 65536;
FILE* report = stdout;
std::atomic<unsigned> failures{0};
std::atomic<unsigned> writable_transitions{0}, executable_transitions{0}, cache_releases{0};
constexpr std::size_t ram_size = std::size_t{4} << 30;
unsigned backing_releases = 0, table_releases = 0;
#ifdef PS5_NATIVE
unsigned direct_releases = 0;
#endif

void record(const char* name, bool pass, std::uint64_t value = 0, bool required = true) {
    if (!pass && required) ++failures;
    if (std::fprintf(report, "%s\t%s\t%llu\n", name, pass ? "PASS" : required ? "FAIL" : "UNSUPPORTED",
                     static_cast<unsigned long long>(value)) < 0 || std::fflush(report) != 0)
        std::abort();
}

struct Memory : A64::UserCallbacks {
    std::array<std::uint32_t, guest.size()> code = guest;
    std::array<bool, data_size / 4096> writable;
    std::uint8_t* bytes;
    A64::Jit* jit = nullptr;
    Common::PageTable* table = nullptr;
    std::uint64_t ticks = 1000;
    unsigned rejected = 0, unexpected = 0, svc = 0, reads = 0, writes = 0;

    explicit Memory(void* p) : bytes(static_cast<std::uint8_t*>(p)) { writable.fill(true); }
    bool bounds(std::uint64_t addr, std::size_t size, bool write) {
        bool valid = addr >= data_base && size <= data_size && addr - data_base <= data_size - size;
        if (table) {
            const auto limit = std::uint64_t{1} << table->GetAddressSpaceBits();
            valid = size && size <= limit && addr <= limit - size;
            if (valid) {
                for (auto page = addr / 4096; page <= (addr + size - 1) / 4096; ++page)
                    valid &= table->entries[page].Pointer(true) != 0;
            }
        }
        if (valid && write && addr >= data_base && addr - data_base <= data_size - size) {
            const auto first = (addr - data_base) / 4096;
            const auto last = (addr - data_base + size - 1) / 4096;
            for (auto page = first; page <= last; ++page) valid &= writable[page];
        }
        if (!valid) {
            ++rejected;
            jit->HaltExecution(HaltReason::MemoryAbort);
        }
        return valid;
    }
    void copy(std::uint64_t addr, void* value, std::size_t size, bool write) {
        auto* cursor = static_cast<std::uint8_t*>(value);
        while (size) {
            const auto count = table ? std::min(size, 4096 - std::size_t(addr % 4096)) : size;
            auto* host = table ? reinterpret_cast<std::uint8_t*>(table->entries[addr / 4096].Pointer(true) + addr)
                               : bytes + addr - data_base;
            if (write) std::memcpy(host, cursor, count);
            else std::memcpy(cursor, host, count);
            addr += count;
            cursor += count;
            size -= count;
        }
    }
    template <class T> T read(std::uint64_t addr) {
        T value{};
        if (bounds(addr, sizeof(T), false)) {
            copy(addr, &value, sizeof(T), false);
            ++reads;
        }
        return value;
    }
    template <class T> void write(std::uint64_t addr, T value) {
        if (bounds(addr, sizeof(T), true)) {
            copy(addr, &value, sizeof(T), true);
            ++writes;
        }
    }
    std::optional<std::uint32_t> MemoryReadCode(std::uint64_t addr) override {
        if (addr >= code_base && addr - code_base < code.size() * 4 && addr % 4 == 0)
            return code[(addr - code_base) / 4];
        return std::nullopt;
    }
    std::uint8_t MemoryRead8(std::uint64_t a) override { return read<std::uint8_t>(a); }
    std::uint16_t MemoryRead16(std::uint64_t a) override { return read<std::uint16_t>(a); }
    std::uint32_t MemoryRead32(std::uint64_t a) override { return read<std::uint32_t>(a); }
    std::uint64_t MemoryRead64(std::uint64_t a) override { return read<std::uint64_t>(a); }
    A64::Vector MemoryRead128(std::uint64_t a) override { return read<A64::Vector>(a); }
    void MemoryWrite8(std::uint64_t a, std::uint8_t v) override { write(a, v); }
    void MemoryWrite16(std::uint64_t a, std::uint16_t v) override { write(a, v); }
    void MemoryWrite32(std::uint64_t a, std::uint32_t v) override { write(a, v); }
    void MemoryWrite64(std::uint64_t a, std::uint64_t v) override { write(a, v); }
    void MemoryWrite128(std::uint64_t a, A64::Vector v) override { write(a, v); }
    void CallSVC(std::uint32_t imm) override {
        if (imm != 0) ++unexpected;
        ++svc;
        jit->HaltExecution();
    }
    void ExceptionRaised(std::uint64_t, A64::Exception) override {
        ++unexpected;
        jit->HaltExecution(HaltReason::UserDefined2);
    }
    void AddTicks(std::uint64_t n) override { ticks -= std::min(n, ticks); }
    std::uint64_t GetTicksRemaining() override { return ticks; }
    std::uint64_t GetCNTPCT() override { return 1000 - ticks; }
    void reset() {
        ticks = 1000;
        rejected = unexpected = svc = reads = writes = 0;
        jit->Reset();
        jit->ClearHalt(~HaltReason{});
        jit->SetPC(code_base);
        jit->SetRegister(0, 7);
        jit->SetRegister(1, 35);
        jit->SetRegister(10, data_base + 4092);
        jit->SetRegister(11, data_base + 16380);
        jit->SetRegister(12, data_base + 32760);
    }
};

void run_cpu(void* data) {
    Memory memory(data);
    A64::UserConfig config{};
    config.callbacks = &memory;
    config.fastmem_pointer = std::nullopt;
    config.page_table = nullptr;
    config.code_cache_size = 16 * 1024 * 1024;
    config.check_halt_on_memory_access = true;
    config.enable_cycle_counting = true;
    record("jit_construct_begin", true);
    {
        A64::Jit jit(config);
        memory.jit = &jit;
        record("jit_constructed", Xbyak::GetError() == 0, Xbyak::GetError());
        if (Xbyak::GetError() != 0) return;
        memory.reset();
        const auto reason = jit.Run();
        record("arm64_halt", reason == HaltReason::UserDefined1 && memory.svc == 1 &&
               memory.unexpected == 0 && memory.rejected == 0 && memory.ticks != 0,
               static_cast<unsigned>(reason));
        record("arm64_arithmetic", jit.GetRegister(2) == 42, jit.GetRegister(2));
        record("arm64_loop", jit.GetRegister(5) == 52 && jit.GetRegister(4) == 0, jit.GetRegister(5));
        record("cross_4k_store_load", memory.read<std::uint64_t>(data_base + 4092) == 42);
        record("cross_16k_store_load", memory.read<std::uint64_t>(data_base + 16380) == 52);
        record("arm64_fp64", std::bit_cast<double>(jit.GetVector(2)[0]) == 3.5);
        const A64::Vector expected{0x0000005400000054ULL, 0x0000005400000054ULL};
        record("arm64_simd_cross_boundary", memory.read<A64::Vector>(data_base + 32760) == expected);
        record("callback_counts", memory.reads >= 2 && memory.writes == 3, memory.writes);

        // Modify the first instruction from ADD to SUB; the assembled opcode is checked on the host.
        memory.code[0] = guest_sub;
        memory.reset();
        jit.InvalidateCacheRange(code_base, 4);
        jit.Run();
        record("code_invalidation", jit.GetRegister(2) == std::uint64_t(7 - 35) &&
               jit.GetRegister(5) == std::uint64_t(7 - 35 + 10) && memory.svc == 1);
        memory.code[0] = guest[0];
        memory.reset();
        jit.ClearCache();
        jit.Run();
        record("code_cache_rebuild", jit.GetRegister(2) == 42 && memory.svc == 1);

        memory.writable[1] = false;
        std::array<std::uint8_t, 8> before{};
        std::memcpy(before.data(), memory.bytes + 4092, before.size());
        memory.reset();
        const auto denied = jit.Run();
        record("guest_4k_write_denied", denied == HaltReason::MemoryAbort && memory.rejected == 1 &&
               memory.writes == 0 && memory.svc == 0);
        record("denied_store_unchanged", std::memcmp(before.data(), memory.bytes + 4092, before.size()) == 0);
        memory.writable[1] = true;
        memory.reset();
        jit.SetRegister(10, data_base + data_size - 4);
        record("out_of_range_rejected", jit.Run() == HaltReason::MemoryAbort &&
               memory.rejected == 1 && memory.writes == 0);
        record("assembler_error_free", Xbyak::GetError() == 0, Xbyak::GetError());
    }
    record("jit_destroyed", cache_releases == 1, cache_releases);
    record("jit_wx_transitions", writable_transitions > 1 && executable_transitions > 1,
           executable_transitions);
}

void run_memory() {
    using Common::HostMemory;
    using Common::PageTable;
    using Common::PageType;
    constexpr std::uint64_t alias = (std::uint64_t{1} << 38) + data_base;
    const auto released_before = backing_releases;
    const auto tables_before = table_releases;
    {
        HostMemory original(ram_size, std::size_t{1} << 39);
        const auto base = original.BackingBasePointer();
        record("backing_host_alignment", reinterpret_cast<std::uintptr_t>(base) % sysconf(_SC_PAGESIZE) == 0);
        HostMemory assigned(data_size, 0);
        assigned = std::move(original);
        HostMemory backing(std::move(assigned));
        record("backing_move_ownership", !original.BackingBasePointer() && !assigned.BackingBasePointer() &&
               backing.BackingBasePointer() == base && !backing.VirtualBasePointer());
        auto* words = reinterpret_cast<volatile std::uint64_t*>(base);
        constexpr std::uint64_t salt = 0xd6e8feb86659fd93ULL;
        for (std::size_t i = 0; i < ram_size / sizeof(*words); ++i) words[i] = salt ^ i;
        bool intact = true;
        for (std::size_t i = 0; i < ram_size / sizeof(*words); ++i) intact &= words[i] == (salt ^ i);
        record("guest_ram_written_verified", intact, ram_size);
        const auto left = base[4095], right = base[8192];
        backing.ClearBackingRegion(4096, 4096, 0xa5);
        record("backing_4k_clear", base[4095] == left && base[8192] == right &&
               std::all_of(base + 4096, base + 8192, [](auto byte) { return byte == 0xa5; }));
        bool rejected = false;
        try { backing.ClearBackingRegion(ram_size - 1, 2, 0); }
        catch (const std::out_of_range&) { rejected = true; }
        record("backing_range_guard", rejected);
        backing.ClearBackingRegion(0, data_size, 0);

        PageTable table;
        table.Resize(39, 12);
        record("eden_page_table_39bit", table.entries.data() && table.entries.size() == (std::size_t{1} << 27) &&
               table.current_page_bits == 12 && !table.fastmem_arena, table.entries.size() * 8);
        if (!table.entries.data()) return;
        auto map = [&](std::uint64_t guest_address, std::size_t physical) {
            if (guest_address % 4096 || guest_address >= (std::uint64_t{1} << 39) ||
                physical % 4096 || physical > ram_size - 4096) std::abort();
            table.entries.GetAndFault(guest_address / 4096).Store(false, PageType::Memory, 0,
                reinterpret_cast<std::uintptr_t>(base + physical) - guest_address);
        };
        for (std::size_t i = 0; i < data_size / 4096; ++i) map(data_base + i * 4096, i * 4096);
        map(data_base + 4096, 65536); // Non-contiguous backing across a 4 KiB guest boundary.
        map(alias, 0);
        record("eden_pointer_roundtrip", table.entries[alias / 4096].Pointer() + alias ==
               reinterpret_cast<std::uintptr_t>(base));
        Memory memory(base);
        memory.table = &table;
        A64::UserConfig config{};
        config.callbacks = &memory;
        config.code_cache_size = 16 * 1024 * 1024;
        config.check_halt_on_memory_access = true;
        config.page_table = reinterpret_cast<void**>(const_cast<PageTable::PageEntryData*>(table.entries.data()));
        config.page_table_address_space_bits = 39;
        config.page_table_pointer_mask = PageTable::ATTRIBUTE_MASK;
        config.page_table_marked_bit = 0;
        config.silently_mirror_page_table = false;
        config.absolute_offset_page_table = true;
        config.detect_misaligned_access_via_page_table = 16 | 32 | 64 | 128;
        config.only_detect_misalignment_via_page_table_on_page_boundary = true;
        // This backend consumes a shift count, not Eden's SIGN_BIT position.
        constexpr auto pointer_shift = std::countl_zero(PageTable::ATTRIBUTE_MASK);
        config.page_table_sign_extension = pointer_shift;
        PageTable::PageEntryData packed;
        const auto negative_bias = std::uint64_t{0x400000} - alias;
        packed.Store(false, PageType::Memory, 0, negative_bias);
        const auto masked = std::bit_cast<std::uint64_t>(packed.Raw()) & PageTable::ATTRIBUTE_MASK;
        const auto decoded = std::bit_cast<std::int64_t>(masked << pointer_shift) >> pointer_shift;
        record("pagetable_negative_bias_decode", std::uint64_t(decoded) == negative_bias &&
               packed.Pointer() == negative_bias, pointer_shift);
        {
            A64::Jit jit(config);
            memory.jit = &jit;
            auto run_aligned = [&](std::uint64_t address) {
                memory.reset();
                jit.SetRegister(10, address + 64);
                jit.SetRegister(11, address + 72);
                jit.SetRegister(12, address + 80);
                return jit.Run();
            };
            const auto alias_halt = run_aligned(alias);
            std::uint64_t stored;
            std::memcpy(&stored, base + 64, sizeof(stored));
            record("pagetable_direct_alias", alias_halt == HaltReason::UserDefined1 && stored == 42 &&
                   jit.GetRegister(5) == 52 && memory.reads == 0 && memory.writes == 0);
            memory.reset();
            const auto boundary_halt = jit.Run();
            const auto boundary_reads = memory.reads, boundary_writes = memory.writes;
            record("pagetable_split_boundary", boundary_halt == HaltReason::UserDefined1 &&
                   memory.read<std::uint64_t>(data_base + 4092) == 42 &&
                   memory.read<std::uint64_t>(data_base + 16380) == 52 &&
                   memory.read<A64::Vector>(data_base + 32760) ==
                       A64::Vector{0x0000005400000054ULL, 0x0000005400000054ULL} &&
                   boundary_reads == 2 && boundary_writes == 3 && base[65536] == 0);
            const std::uint64_t sentinel = 99;
            std::memcpy(base + 64, &sentinel, sizeof(sentinel));
            map(alias, 36864);
            const auto remap_halt = run_aligned(alias);
            std::memcpy(&stored, base + 36864 + 64, sizeof(stored));
            record("pagetable_live_remap", remap_halt == HaltReason::UserDefined1 && stored == 42 &&
                   std::memcmp(base + 64, &sentinel, sizeof(sentinel)) == 0 && memory.writes == 0);
            table.entries.GetAndFault(alias / 4096).MarkDebug(
                reinterpret_cast<std::uintptr_t>(base + 36864) - alias, 0);
            const auto marked_halt = run_aligned(alias);
            record("pagetable_marked_callbacks", marked_halt == HaltReason::UserDefined1 &&
                   memory.reads == 2 && memory.writes == 3 && memory.unexpected == 0);
            table.entries.GetAndFault(alias / 4096).Store(false, PageType::Unmapped, 0, 0);
            const auto unmapped_halt = run_aligned(alias);
            record("pagetable_unmapped_callback", unmapped_halt == HaltReason::MemoryAbort &&
                   memory.rejected == 1 && memory.writes == 0 && memory.svc == 0);
            record("pagetable_assembler_error_free", Xbyak::GetError() == 0, Xbyak::GetError());
        }
        record("pagetable_jit_release", cache_releases == 2, cache_releases);
    }
    record("guest_ram_release", backing_releases == released_before + 1, backing_releases - released_before);
    record("eden_page_table_release", table_releases == tables_before + 1, table_releases - tables_before);
#ifdef PS5_NATIVE
    record("native_direct_releases", direct_releases == 3, direct_releases);
#else
    record("native_direct_releases", false, 0, false);
#endif
}

std::atomic<unsigned> tls_destructors{0};
struct ThreadState {
    unsigned value = 0;
    ~ThreadState() { ++tls_destructors; }
};
thread_local ThreadState thread_state;

struct ConcurrentMemory final : Memory {
    std::atomic<std::uint64_t>& counter;
    Common::Barrier& reservations;
    std::stop_token token;
    unsigned rendezvous = 0;
    ConcurrentMemory(void* data, std::atomic<std::uint64_t>& counter_, Common::Barrier& barrier,
                     std::stop_token token_) : Memory(data), counter(counter_), reservations(barrier), token(token_) {}
    std::optional<std::uint32_t> MemoryReadCode(std::uint64_t addr) override {
        if (addr >= code_base && addr - code_base < multicore_guest.size() * 4 && addr % 4 == 0)
            return multicore_guest[(addr - code_base) / 4];
        return std::nullopt;
    }
    std::uint64_t MemoryRead64(std::uint64_t addr) override {
        if (addr != data_base) { ++unexpected; jit->HaltExecution(HaltReason::MemoryAbort); return 0; }
        ++reads;
        return counter.load();
    }
    bool MemoryWriteExclusive64(std::uint64_t addr, std::uint64_t value, std::uint64_t expected) override {
        if (addr != data_base) { ++unexpected; jit->HaltExecution(HaltReason::MemoryAbort); return false; }
        const bool stored = counter.compare_exchange_strong(expected, value);
        writes += stored;
        return stored;
    }
    void CallSVC(std::uint32_t imm) override {
        if (imm == 1) {
            ++rendezvous;
            if (!reservations.Sync(token)) { ++unexpected; jit->HaltExecution(); }
        } else {
            Memory::CallSVC(imm);
        }
    }
};

void run_multicore() {
    constexpr auto cores = Core::Hardware::NUM_CPU_CORES;
    static_assert(cores == 4);
    const auto cache_before = cache_releases.load();
    const auto tls_before = tls_destructors.load();
    std::array<std::uint64_t, cores> guest_tls{101, 102, 103, 104};
    std::array<std::uint64_t, cores> retry_counts{};
    std::array<bool, cores> ran{}, tls_ok{}, stopped{}, failed{};
    std::array<unsigned, cores> fiber_steps{};
    record("multicore_begin", true, cores);
    {
        Common::HostMemory backing(data_size, 0);
        auto* counter = new (backing.BackingBasePointer()) std::atomic<std::uint64_t>{0};
        SCOPE_EXIT { std::destroy_at(counter); };
        ExclusiveMonitor monitor(cores);
        Common::Barrier start(cores + 1), finish(cores + 1), reservations(cores);
        std::stop_source cancel;
        std::array<std::unique_ptr<ConcurrentMemory>, cores> memory;
        std::array<std::unique_ptr<A64::Jit>, cores> jits;
        for (unsigned i = 0; i < cores; ++i) {
            memory[i] = std::make_unique<ConcurrentMemory>(backing.BackingBasePointer(), *counter,
                                                         reservations, cancel.get_token());
            A64::UserConfig config{};
            config.callbacks = memory[i].get();
            config.code_cache_size = 16 * 1024 * 1024;
            config.processor_id = i;
            config.global_monitor = &monitor;
            config.tpidr_el0 = &guest_tls[i];
            config.check_halt_on_memory_access = true;
            jits[i] = std::make_unique<A64::Jit>(config);
            memory[i]->jit = jits[i].get();
            memory[i]->reset();
            memory[i]->ticks = 1'000'000;
            jits[i]->SetRegister(10, data_base);
        }
        std::array<std::jthread, cores> workers;
        // Cancel the shared rendezvous before joining if construction throws.
        SCOPE_EXIT { cancel.request_stop(); };
        for (unsigned i = 0; i < cores; ++i) {
            workers[i] = std::jthread([&, i](std::stop_token stop) {
                try {
                    thread_state.value = i + 1;
                    auto host = Common::Fiber::ThreadToFiber();
                    SCOPE_EXIT { host->Exit(); };
                    std::shared_ptr<Common::Fiber> fiber;
                    fiber = std::make_shared<Common::Fiber>([&] {
                        ++fiber_steps[i];
                        Common::Fiber::YieldTo(fiber, *host);
                        try {
                            const auto reason = jits[i]->Run();
                            ran[i] = reason == HaltReason::UserDefined1 && memory[i]->svc == 1 &&
                                memory[i]->rendezvous == 1 && memory[i]->unexpected == 0 &&
                                memory[i]->ticks != 0 && memory[i]->writes == 4096 &&
                                jits[i]->GetRegister(8) == guest_tls[i] && jits[i]->GetRegister(9) == 0;
                            retry_counts[i] = jits[i]->GetRegister(7);
                        } catch (...) { failed[i] = true; cancel.request_stop(); }
                        tls_ok[i] = thread_state.value == i + 1;
                        ++fiber_steps[i];
                        Common::Fiber::YieldTo(fiber, *host);
                        std::abort(); // Eden fiber entry points must not return.
                    });
                    if (!start.Sync(cancel.get_token())) return;
                    Common::Fiber::YieldTo(host, *fiber);
                    Common::Fiber::YieldTo(host, *fiber);
                    if (!finish.Sync(cancel.get_token())) return;
                    stopped[i] = !Common::StoppableTimedWait(stop, std::chrono::seconds(5));
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "Worker %u: %s\n", i, error.what());
                    failed[i] = true;
                    cancel.request_stop();
                }
            });
        }
        const bool started = start.Sync(cancel.get_token());
        const bool finished = started && finish.Sync(cancel.get_token());
        for (auto& worker : workers) worker.request_stop();
        for (auto& worker : workers) worker.join();
        const auto all = [](const auto& values) { return std::all_of(values.begin(), values.end(), [](auto v) { return bool(v); }); };
        record("multicore_joined", started && finished && !std::any_of(failed.begin(), failed.end(), [](bool f) { return f; }), cores);
        record("multicore_guest_results", all(ran));
        record("multicore_atomic_counter", counter->load() == cores * 4096, counter->load());
        std::uint64_t retries = 0;
        for (auto value : retry_counts) retries += value;
        record("multicore_exclusive_contention", retries >= cores - 1, retries);
        record("multicore_fiber_yields", std::all_of(fiber_steps.begin(), fiber_steps.end(), [](auto n) { return n == 2; }));
        record("multicore_tls_isolation", all(tls_ok));
        record("multicore_stop_wakeup", all(stopped));
    }
    record("multicore_tls_destructors", tls_destructors == tls_before + cores, tls_destructors - tls_before);
    record("multicore_cache_release", cache_releases == cache_before + cores, cache_releases - cache_before);
#ifdef PS5_NATIVE
    record("multicore_backing_release", direct_releases == 4, direct_releases);
#else
    record("multicore_backing_release", true);
#endif
}
} // namespace

extern "C" int __real_mprotect(void*, std::size_t, int);
extern "C" int __real_munmap(void*, std::size_t);
extern "C" void* __wrap_aligned_alloc(std::size_t, std::size_t);
#ifdef PS5_NATIVE
extern "C" void ps5_opengl_heap_snapshot(const char*, unsigned);
extern "C" std::int32_t __real_sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
extern "C" std::int32_t __wrap_sceKernelReleaseDirectMemory(std::int64_t physical, std::size_t size) {
    const auto rc = __real_sceKernelReleaseDirectMemory(physical, size);
    if (rc == 0) ++direct_releases;
    return rc;
}
#endif
extern "C" int __wrap_mprotect(void* address, std::size_t size, int protection) {
    const int result = __real_mprotect(address, size, protection);
    if (result != 0 || ((protection & PROT_WRITE) && (protection & PROT_EXEC))) {
        record("jit_protection_failure", false, errno);
        std::abort();
    }
    writable_transitions += (protection & PROT_WRITE) != 0;
    executable_transitions += (protection & PROT_EXEC) != 0;
    return result;
}
extern "C" int __wrap_munmap(void* address, std::size_t size) {
    const int result = __real_munmap(address, size);
    if (result == 0 && size == 16 * 1024 * 1024) ++cache_releases;
    const auto page = sysconf(_SC_PAGESIZE);
    if (result == 0 && size == ram_size + page) ++backing_releases;
    if (result == 0 && size == (std::size_t{1} << 30) + page) ++table_releases;
    return result;
}

int main() {
#ifdef PS5_NATIVE
    report = std::fopen("/download0/eden-cpu-probe.tsv", "w");
    if (!report) return 2;
    if (!std::freopen("/download0/eden-cpu-errors.log", "w", stderr)) return 2;
    if (!std::freopen("/download0/eden-cpu-heap.log", "w", stdout)) return 2;
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    ps5_opengl_heap_snapshot("before_state", 0);
#endif
    const long pages = sysconf(_SC_PAGESIZE);
    record("host_page_size", pages > 0, pages);
    record("eden_4k_fastmem_compatible", pages == 4096, pages, false);
    // This criterion describes the unmodified fastmem path, not callback-mode feasibility.
    void* data = mmap(nullptr, data_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    record("owned_memory", data != MAP_FAILED, data_size);
    if (data != MAP_FAILED) {
        std::memset(data, 0, data_size);
        // Exact size/alignment of Jit::Impl in this frozen native build.
        // Call the bridge directly so malloc/free pair elimination cannot remove this check.
        void* state = __wrap_aligned_alloc(4096, 0x1004000);
        bool allocated = state && reinterpret_cast<std::uintptr_t>(state) % 4096 == 0;
        if (allocated) {
            auto* observed = static_cast<volatile std::uint8_t*>(state);
            observed[0] = 0x5a;
            observed[0x1003fff] = 0xa5;
            allocated = observed[0] == 0x5a && observed[0x1003fff] == 0xa5;
        }
        record("jit_state_allocation", allocated, state ? 0x1004000 : errno);
        std::free(state);
#ifdef PS5_NATIVE
        ps5_opengl_heap_snapshot("after_state", 0);
#endif
        errno = 0;
        void* invalid = __wrap_aligned_alloc(3, 64);
        const bool invalid_alignment = !invalid && errno == EINVAL;
        std::free(invalid);
        errno = 0;
        invalid = __wrap_aligned_alloc(4096, 1);
        record("allocation_rejection", invalid_alignment && !invalid && errno == EINVAL);
        std::free(invalid);
        if (allocated) {
            try { run_cpu(data); }
            catch (const std::exception& error) {
                std::fprintf(stderr, "JIT exception: %s; RW=%u RX=%u cache_releases=%u\n",
                             error.what(), writable_transitions.load(), executable_transitions.load(), cache_releases.load());
                record("jit_exception", false);
            }
        }
#ifdef PS5_NATIVE
        ps5_opengl_heap_snapshot("after_jit", 0);
#endif
        record("owned_memory_release", munmap(data, data_size) == 0);
    }
    constexpr std::size_t arena_size = std::size_t{1} << 39;
    void* arena = mmap(nullptr, arena_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const int reserve_error = errno;
    record("fastmem_virtual_reservation", arena != MAP_FAILED, arena == MAP_FAILED ? reserve_error : arena_size, false);
    if (arena != MAP_FAILED) record("virtual_reservation_release", munmap(arena, arena_size) == 0);
    try { run_memory(); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Memory adapter exception: %s\n", error.what());
        record("memory_adapter_exception", false);
    }
#ifdef PS5_NATIVE
    ps5_opengl_heap_snapshot("after_memory", 0);
#endif
    if (!failures) {
        try { run_multicore(); }
        catch (const std::exception& error) {
            std::fprintf(stderr, "Multicore exception: %s\n", error.what());
            record("multicore_exception", false);
        }
    }
#ifdef PS5_NATIVE
    ps5_opengl_heap_snapshot("after_multicore", 0);
#endif
    const unsigned result = failures;
    record("CPU_PROBE_COMPLETE", result == 0, result);
    if (report != stdout && std::fclose(report) != 0) return 2;
    return result == 0 ? 0 : 1;
}
