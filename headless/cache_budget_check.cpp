// SPDX-License-Identifier: GPL-3.0-or-later
#include "cache_budget.h"
#include <cassert>
#include <fstream>
#include <unistd.h>
int main() {
    char directory[] = "/tmp/eden-cache-XXXXXX";
    assert(mkdtemp(directory));
    const std::filesystem::path root{directory};
    for (auto name : {"abc.bin", "ffff.bin", "prod.keys", "save.bin", "abc.bin.tmp", "runtime"})
        std::ofstream(root / name) << "record";
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(root)) entries.push_back(entry);
    assert(Eden::TrimShaderCache(entries, 1024 * 1024) == 0);
    assert(Eden::TrimShaderCache(entries, 0) == 2);
    assert(Eden::TrimShaderCache(entries, 0) == 0);
    for (auto name : {"prod.keys", "save.bin", "abc.bin.tmp", "runtime"}) assert(std::filesystem::exists(root/name));
    std::filesystem::remove_all(root);
}