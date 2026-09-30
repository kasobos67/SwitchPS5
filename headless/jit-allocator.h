// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <sys/mman.h>
#include <unistd.h>
#include <xbyak/xbyak.h>
namespace Common {
void* AllocateMemoryPages(std::size_t) noexcept;
void FreeMemoryPages(void*) noexcept;
void* MapExecutableAlias(void*, std::size_t) noexcept;
}
// Separate RW/NX and RX views avoid changing page permissions per compiled block.
// Unsupported native JIT allocation falls back to the qualified owned W^X path.
inline Xbyak::Allocator* EdenJitAllocator() {
    struct Allocator final : Xbyak::Allocator {
        struct Mapping { void* writable; std::size_t size; };
        std::mutex mutex;
        std::unordered_map<std::uint8_t*, Mapping> mappings;
        std::uint8_t* alloc(std::size_t size) override {
#ifdef EDEN_JIT_ALIAS_NATIVE
            auto* diagnostics = stdout; // Native stdout is the separate heap log.
#else
            auto* diagnostics = stderr; // Host stdout is the strict lifecycle receipt.
#endif
            const long page = sysconf(_SC_PAGESIZE);
            if (!size || page <= 0 || size > std::numeric_limits<std::size_t>::max() - page)
                return nullptr;
            const auto span = (size + page - 1) & ~(std::size_t(page) - 1);
            void* executable = MAP_FAILED;
            void* writable = MAP_FAILED;
#ifdef EDEN_JIT_ALIAS_NATIVE
            writable = Common::AllocateMemoryPages(size);
            if (!writable) return nullptr;
            executable = Common::MapExecutableAlias(writable, size);
            if (!executable) {
                std::fprintf(diagnostics, "EDEN_JIT_ALIAS bytes=%zu active=0 errno=%d\n", span, errno);
                return static_cast<std::uint8_t*>(writable);
            }
#else
            const int executable_fd = memfd_create("eden-jit", MFD_CLOEXEC);
            if (executable_fd >= 0 && ftruncate(executable_fd, span) == 0) {
                executable = mmap(nullptr, span, PROT_READ | PROT_EXEC, MAP_SHARED, executable_fd, 0);
                writable = mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_SHARED, executable_fd, 0);
            }
            if (executable_fd >= 0 && close(executable_fd) != 0) std::abort();
#endif
            if (executable != MAP_FAILED && executable && writable != MAP_FAILED && writable && executable != writable) {
                auto* pointer = static_cast<std::uint8_t*>(executable);
                try {
                    std::lock_guard lock(mutex);
                    mappings.emplace(pointer, Mapping{writable, span});
                    std::fprintf(diagnostics, "EDEN_JIT_ALIAS bytes=%zu active=1\n", span);
                    return pointer;
                } catch (...) {
                    // Release both views before reporting allocation failure.
                    if (munmap(executable, span) != 0) std::abort();
#ifdef EDEN_JIT_ALIAS_NATIVE
                    Common::FreeMemoryPages(writable);
#else
                    if (munmap(writable, span) != 0) std::abort();
#endif
                    return nullptr;
                }
            }
#ifdef EDEN_JIT_ALIAS_NATIVE
            std::abort(); // Distinct views are required by the direct mapper.
#else
            if (executable != MAP_FAILED && executable && munmap(executable, span) != 0) std::abort();
            if (writable != MAP_FAILED && writable && writable != executable && munmap(writable, span) != 0) std::abort();
            std::fprintf(diagnostics, "EDEN_JIT_ALIAS bytes=%zu active=0 errno=%d\n", span, errno);
            return static_cast<std::uint8_t*>(Common::AllocateMemoryPages(size));
#endif
        }
        std::uint8_t* writableAddress(std::uint8_t* pointer) override {
            std::lock_guard lock(mutex);
            const auto entry = mappings.find(pointer);
            return entry == mappings.end() ? pointer : static_cast<std::uint8_t*>(entry->second.writable);
        }
        void free(std::uint8_t* pointer) override {
            {
                std::lock_guard lock(mutex);
                const auto entry = mappings.find(pointer);
                if (entry != mappings.end()) {
                    const auto mapping = entry->second;
                    if (munmap(pointer, mapping.size) != 0) std::abort();
#ifdef EDEN_JIT_ALIAS_NATIVE
                    Common::FreeMemoryPages(mapping.writable);
#else
                    if (munmap(mapping.writable, mapping.size) != 0) std::abort();
#endif
                    mappings.erase(entry);
                    return;
                }
            }
            Common::FreeMemoryPages(pointer);
        }
        bool useProtect() const override { return false; }
    };
    static Allocator allocator;
    return &allocator;
}
