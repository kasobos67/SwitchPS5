// SPDX-License-Identifier: GPL-3.0-or-later
// Host check of the patch library (patch_library.h). Build: c++ -std=c++20 patch_library_check.cpp
#include "patch_library.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace Eden::Patches;
    // Grouping: frame rates and per-mode resolutions exclude each other; the rest combine.
    assert(GroupKey("60 FPS") == "fps" && GroupKey("OG 30 FPS") == "fps" && GroupKey("120(?) FPS For Emulator") == "fps");
    assert(GroupKey("Dynamic Resolution 60 FPS tweak") == "dynamic fps");
    assert(GroupKey("Docked RRS 90% (810p) v2") == "resolution docked" && GroupKey("Docked 1080p") == "resolution docked");
    assert(GroupKey("Handheld 480p") == "resolution handheld");
    assert(GroupKey("Disable RRS Lock for Docked (Re-enable DRS)").empty() && GroupKey("Main FOV 90").empty());

    // Parsing: entries with code only; the master code is no entry.
    const char* text =
        "{Master Code}\r\n580F0000 01234567\r\n\r\n[--SectionStart:Frame rate--]\n"
        "[60 FPS]\n04000000 00ABCDEF 52800020\n[30 FPS]\n04000000 00ABCDEF 52800040\n"
        "[Docked 900p]\n04000000 00111111 00000384\n";
    auto entries = ParseCheats(text, "Collection");
    assert(entries.size() == 3 && entries[0].name == "60 FPS" && entries[2].name == "Docked 900p");
    assert(entries[0].id == "Collection#60 FPS" && entries[0].source == "Collection");
    assert(entries[0].group == "fps" && entries[1].group == "fps" && entries[2].group == "resolution docked");

    // Choosing: 60 then 30 FPS keeps only 30; resolution combines.
    std::set<std::string> chosen;
    Toggle(entries, chosen, 0);
    Toggle(entries, chosen, 2);
    Toggle(entries, chosen, 1);
    assert(chosen.size() == 2 && !chosen.contains(entries[0].id) && chosen.contains(entries[1].id));
    Toggle(entries, chosen, 1);
    assert(!chosen.contains(entries[1].id));
    std::printf("patch library: all checks passed\n");
}
