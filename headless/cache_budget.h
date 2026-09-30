// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace Eden {
// Call only between sessions, when no compiler can be writing cache records.
inline size_t TrimShaderCache(const std::vector<std::filesystem::directory_entry>& entries,
                              uint64_t budget = 64 * 1024 * 1024) {
    struct Record { std::filesystem::path path; uint64_t bytes; time_t modified; };
    std::vector<Record> records;
    uint64_t total = 0;
    for (const auto& entry : entries) {
        const auto name = entry.path().filename().string();
        const auto dot = name.find('.');
        if ((dot != 3 && dot != 4) || name.substr(dot) != ".bin" ||
            name.find_first_not_of("0123456789abcdef") != dot) continue;
        struct stat info{};
        if (::stat(entry.path().c_str(), &info) || !S_ISREG(info.st_mode) ||
            info.st_blocks < 0 || info.st_size < 0) continue;
        const auto bytes = std::max(uint64_t(info.st_size), uint64_t(info.st_blocks) * 512);
        records.push_back({entry.path(), bytes, info.st_mtime});
        total += bytes;
    }
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
        return a.modified < b.modified;
    });
    size_t removed = 0;
    for (const auto& record : records) {
        if (total <= budget) break;
        std::error_code error;
        if (std::filesystem::remove(record.path, error)) {
            total -= record.bytes;
            ++removed;
        }
    }
    return removed;
}
}
