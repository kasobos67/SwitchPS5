// SPDX-License-Identifier: GPL-3.0-or-later
// Cheat files as players download them: cheats/<first 16 hex digits of the build ID>.txt, the
// layout cheat collections come in. Each [entry] is chosen on its own; the file's {master} code
// comes with them. Nothing here ships third-party cheats; it reads what the player copied in.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Eden::Patches {
struct Entry {
    std::string id;     // saved in the settings: source + "#" + entry name
    std::string name;   // shown
    std::string source; // where it comes from: entries of one source are chosen together
    std::string group;  // entries of one source with the same group exclude each other
};

inline std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline std::string Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return std::string(text);
}

// Choices that replace each other: frame rates ("60 FPS", "30 FPS"), and resolutions per mode
// ("Docked 900p", "Docked RRS 90% (810p)"). Other entries combine freely.
inline std::string GroupKey(std::string_view name) {
    const std::string lower = Lower(name);
    std::vector<std::string> words;
    std::string word;
    for (const char c : lower + " ") {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            word += c;
        } else if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    }
    const auto has = [&](std::string_view w) { return std::find(words.begin(), words.end(), w) != words.end(); };
    bool fps = false, resolution = has("rrs") || has("resolution");
    for (const auto& w : words) {
        fps |= w == "fps" || (w.size() > 3 && w.ends_with("fps") && std::isdigit(static_cast<unsigned char>(w[0])));
        resolution |= w.size() > 1 && w.back() == 'p' &&
                      std::all_of(w.begin(), w.end() - 1, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    }
    if (fps) return has("dynamic") ? "dynamic fps" : "fps";
    if (resolution && !has("disable") && !has("lock"))
        return has("handheld") ? "resolution handheld" : has("docked") ? "resolution docked" : "resolution";
    return {};
}

// The [entries] of a cheat file; entries without code (section titles) are left out.
inline std::vector<Entry> ParseCheats(std::string_view text, const std::string& source) {
    std::vector<Entry> entries;
    bool current = false;
    bool in_master = false;
    bool current_has_code = false;
    const auto finish = [&] {
        if (current && !current_has_code) entries.pop_back();
        current = false;
    };
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string line = Trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty()) {
            if (end == text.size()) break;
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            finish();
            in_master = false;
            Entry entry;
            entry.name = Trim(std::string_view(line).substr(1, line.size() - 2));
            entry.source = source;
            entry.id = source + "#" + entry.name;
            entry.group = GroupKey(entry.name);
            entries.push_back(std::move(entry));
            current = true;
            current_has_code = false;
        } else if (line.front() == '{' && line.back() == '}') {
            finish();
            in_master = true;
        } else if (!in_master && current) {
            current_has_code |= std::isxdigit(static_cast<unsigned char>(line.front())) != 0;
        }
        if (end == text.size()) break;
    }
    finish();
    return entries;
}

// Picking `index` clears the other entries of its group in the same source.
inline void Toggle(const std::vector<Entry>& entries, std::set<std::string>& chosen, std::size_t index) {
    const Entry& entry = entries.at(index);
    if (chosen.erase(entry.id)) return;
    if (!entry.group.empty())
        for (const auto& other : entries)
            if (other.source == entry.source && other.group == entry.group) chosen.erase(other.id);
    chosen.insert(entry.id);
}
} // namespace Eden::Patches
