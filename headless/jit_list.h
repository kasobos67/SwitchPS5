// SPDX-License-Identifier: GPL-3.0-or-later
// Block list: the code blocks a game's last sessions compiled, compiled again on a spare CPU while
// the next session starts, so the game finds them ready instead of pausing to compile them at
// its first use of each (the dips when a game enters new areas).
//
// The JIT records the location of every block it publishes (headless/dynarmic/jit_group.h,
// history). When a game stops, its list is saved in <user>/cache/jit/<program ID>.blocks:
//
//   "EJL1", the game's build ID (32 bytes), the number of entries (64-bit), then one 64-bit
//   value per block: the block's mode bits (the top byte of the JIT's location descriptor) and
//   its address as an offset from where the game's code starts.
//
// Addresses are offsets because the code is loaded at a different address every session. Only
// blocks in the game's own modules are kept (the code mapped when it starts): code a game maps
// later, such as plug-in modules, lands at another address every session, and the game may be
// mapping it while a block there is read. A list is used only for the build it was saved from. A
// session's own blocks come first, in the order it used them; blocks only earlier sessions used
// follow, so the list grows to cover every part of the game that was played.
//
// Compiling ahead never makes the game wait (a guest core that wants to compile goes first) and
// never fills the code cache: it stops while each region keeps a reserve (jit_impl.inc,
// EdenPrecompile). The blocks are ordinary JIT blocks, invalidated like any other.
//
// Measured on the console in a large open-world game (the same seven-minute walk twice): 518,130
// blocks listed (4 MB); 503,120 compiled ahead in 7.8 s on the spare CPU; the guest cores then
// compiled 426 blocks during play instead of 138,262, and the 5-second window in which gameplay
// starts ran at 30.0 FPS instead of 22.3.
//
// Off unless "block_list" is true under "performance" in the settings file (Settings >
// Performance > Compile ahead; settings_store.h) or a file named block-list.txt is in the app
// folder; in a development build dev-settings jit_list=on/off goes before the settings. It has
// run in one game on the console so far. Only for 64-bit games: the shared JIT the list hangs on
// does not cover 32-bit ones.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <sys/stat.h>

#ifndef EDEN_JIT_LIST_FORMAT_ONLY  // a host check includes only the file format
#include "performance.h"
#include "storage_paths.h"

extern "C" void* eden_jit_list_open();
extern "C" void eden_jit_list_close(void* handle);
extern "C" int eden_jit_precompile(void* handle, unsigned long long location);
extern "C" std::size_t eden_jit_history(void* handle, unsigned long long* out, std::size_t capacity);
#endif

namespace Eden::JitList {
inline std::atomic<bool> enabled{false};

using BuildId = std::array<unsigned char, 32>;
using Value = unsigned long long;
inline constexpr Value kAddressMask = (Value{1} << 56) - 1;
inline constexpr Value kSingleStep = Value{1} << 57;
inline constexpr Value kSpan = Value{1} << 32;
inline constexpr std::size_t kLimit = 1'500'000;  // entries kept in a file (12 MB)

// The entries for the locations a session published, first use first, without repeats; then
// those of `earlier` (a loaded list) that the session did not use. `image` is the size of the
// game's own modules from code_start; blocks outside them are left out.
inline std::vector<Value> Encode(const std::vector<Value>& locations, Value code_start, Value image,
                                 const std::vector<Value>& earlier = {}) {
    std::vector<Value> entries;
    std::unordered_set<Value> seen;
    entries.reserve(std::min(kLimit, locations.size() + earlier.size()));
    seen.reserve(std::min(kLimit, locations.size() + earlier.size()));
    image = std::min(image, kSpan);
    const auto add = [&](Value entry) {
        if (entries.size() < kLimit && (entry & kAddressMask) < image && seen.insert(entry).second)
            entries.push_back(entry);
    };
    for (const Value location : locations) {
        const Value address = location & kAddressMask;
        if ((location & kSingleStep) || address < code_start) continue;
        add((location & ~kAddressMask) | (address - code_start));
    }
    for (const Value entry : earlier) add(entry);
    return entries;
}

// The location an entry names in a session whose code starts at code_start.
inline Value Decode(Value entry, Value code_start) {
    return (entry & ~kAddressMask) | ((code_start + (entry & kAddressMask)) & kAddressMask);
}

inline bool Save(const std::string& path, const BuildId& build, const std::vector<Value>& entries) {
    const std::string fresh = path + ".new";
    std::FILE* file = std::fopen(fresh.c_str(), "wb");
    if (!file) return false;
    const std::uint64_t count = entries.size();
    bool ok = std::fwrite("EJL1", 1, 4, file) == 4 && std::fwrite(build.data(), 1, build.size(), file) == build.size() &&
              std::fwrite(&count, sizeof(count), 1, file) == 1;
    static_assert(sizeof(Value) == 8);
    ok = ok && (entries.empty() || std::fwrite(entries.data(), 8, entries.size(), file) == entries.size());
    ok = std::fclose(file) == 0 && ok;
    if (!ok || std::rename(fresh.c_str(), path.c_str()) != 0) {
        std::remove(fresh.c_str());
        return false;
    }
    return true;
}

// Empty when there is no list, it is another build's, or it is damaged.
inline std::vector<Value> Load(const std::string& path, const BuildId& build) {
    std::vector<Value> entries;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return entries;
    char magic[4]{};
    BuildId saved{};
    std::uint64_t count = 0;
    if (std::fread(magic, 1, 4, file) == 4 && std::memcmp(magic, "EJL1", 4) == 0 &&
        std::fread(saved.data(), 1, saved.size(), file) == saved.size() && saved == build &&
        std::fread(&count, sizeof(count), 1, file) == 1 && count <= kLimit) {
        entries.resize(count);
        if (count && std::fread(entries.data(), 8, count, file) != count) entries.clear();
    }
    std::fclose(file);
    return entries;
}

#ifndef EDEN_JIT_LIST_FORMAT_ONLY
// One game session: Start once the game is loaded and its JITs exist, Finish before they go.
// `image` is the size of the game's own modules from code_start.
class Session {
public:
    void Start(Value program, const BuildId& build, Value code_start, Value image) {
        if (!enabled.load(std::memory_order_relaxed)) return;
        handle_ = eden_jit_list_open();
        if (!handle_) {
            std::puts("EDEN_JIT_LIST unavailable (a 32-bit game, or no shared application JIT)");
            return;
        }
        const std::string folder = Eden::UserDir() + "/cache/jit";
        (void)mkdir((Eden::UserDir() + "/cache").c_str(), 0777);
        (void)mkdir(folder.c_str(), 0777);
        char name[32];
        std::snprintf(name, sizeof(name), "/%016llX.blocks", program);
        path_ = folder + name;
        build_ = build;
        code_start_ = code_start;
        image_ = std::min(image, kSpan);
        earlier_ = Load(path_, build);
        // A list saved before only the game's own modules were kept may name code outside them.
        std::erase_if(earlier_, [this](Value entry) { return (entry & kAddressMask) >= image_; });
        std::printf("EDEN_JIT_LIST loaded=%zu code_start=%llx image=%llx\n", earlier_.size(), code_start, image_);
        if (earlier_.empty()) return;
        worker_ = std::jthread([this](std::stop_token stop) {
#ifdef PS5_NATIVE
            Eden::Performance::RegisterWorker("JitList");  // a spare CPU, off the guest cores
#endif
            const auto start = std::chrono::steady_clock::now();
            std::size_t compiled = 0, ready = 0, busy = 0, at = 0;
            int ended = 0;
            while (at < earlier_.size() && !stop.stop_requested()) {
                const int result = eden_jit_precompile(handle_, Decode(earlier_[at], code_start_));
                if (result == 2) {
                    // Every region with room has its core compiling: let them, then try again.
                    ++busy;
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    continue;
                }
                if (result < 0) {
                    ended = result;
                    break;
                }
                compiled += result == 1;
                ready += result == 0;
                ++at;
            }
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::printf("EDEN_JIT_LIST precompiled=%zu already=%zu of=%zu busy=%zu ended=%d ms=%lld\n", compiled, ready,
                        earlier_.size(), busy, ended, static_cast<long long>(ms));
            std::fflush(stdout);
        });
    }

    void Finish() {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        if (!handle_) return;
        const auto start = std::chrono::steady_clock::now();
        std::vector<Value> locations(eden_jit_history(handle_, nullptr, 0));
        locations.resize(std::min(locations.size(), eden_jit_history(handle_, locations.data(), locations.size())));
        eden_jit_list_close(handle_);
        handle_ = nullptr;
        const std::vector<Value> entries = Encode(locations, code_start_, image_, earlier_);
        const bool saved = !entries.empty() && Save(path_, build_, entries);
        std::printf("EDEN_JIT_LIST saved=%d entries=%zu session=%zu earlier=%zu ms=%lld\n", saved, entries.size(),
                    locations.size(), earlier_.size(),
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count()));
    }

    ~Session() { Finish(); }

private:
    void* handle_ = nullptr;
    std::string path_;
    BuildId build_{};
    Value code_start_ = 0;
    Value image_ = 0;
    std::vector<Value> earlier_;
    std::jthread worker_;
};
#endif
} // namespace Eden::JitList
