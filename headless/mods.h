// SPDX-License-Identifier: GPL-3.0-or-later
// Mods: what the player put in the game files folder's mods/ (next to roms/), in the layout Eden's
// patch manager reads and mod archives come in:
//
//   mods/<title ID, 16 hex digits>/<mod name>/exefs/    code patches (*.pchtxt, *.ips) or whole
//                                                       replacement files (main, subsdk0, ...)
//                                           /romfs/     game files that replace the game's own
//                                           /romfs_ext/ (also romfslite/)
//                                           /cheats/    <build ID>.txt
//   mods/<title ID>/cheat_<anything>.txt                 cheats without a folder
//
// Eden applies every mod of the running game, in the order of their names, except those the
// launcher switched off (settings_store.h keeps their names per game; main.cpp hands them to
// Eden). This header only looks: the launcher lists a game's mods with it and main.cpp names
// them in the log. The title's folder is found whatever the case of its letters (the derived
// bis_factory.cpp does the same for Eden), and is never created behind the player's back.
#pragma once
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__PROSPERO__)
#include "native_directory.h"
#endif

namespace Eden::Mods {
namespace fs = std::filesystem;

enum : unsigned {
    kCode = 1,    // exefs: patches or replacement code
    kFiles = 2,   // romfs: replacement game files
    kCheats = 4,  // cheats
};

struct Mod {
    std::string name;  // the folder's (or cheat file's) name: what Eden's disabled list holds
    unsigned kinds = 0;
};

inline std::string Lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

inline std::string TitleName(uint64_t title_id) {
    char name[17];
    std::snprintf(name, sizeof(name), "%016" PRIX64, title_id);
    return name;
}

inline std::vector<fs::directory_entry> ListFolder(const fs::path& folder) {
    std::error_code error;
    if (!fs::is_directory(folder, error) || error) return {};
#if defined(__PROSPERO__)
    // The console's directory iterator needs this reader (native_directory.h).
    auto entries = Eden::ReadNativeDirectory(folder, error);
    if (error) entries.clear();
    return entries;
#else
    std::vector<fs::directory_entry> entries;
    for (fs::directory_iterator it{folder, error}, end; !error && it != end; it.increment(error))
        entries.push_back(*it);
    return entries;
#endif
}

inline bool IsFolder(const fs::directory_entry& entry) {
    std::error_code error;
    return entry.is_directory(error) && !error;
}

// The game's folder in mods/ (root), or empty when it has none.
inline std::string TitleFolder(const std::string& root, uint64_t title_id) {
    if (title_id == 0) return {};
    const std::string wanted = Lower(TitleName(title_id));
    for (const auto& entry : ListFolder(root))
        if (IsFolder(entry) && Lower(entry.path().filename().string()) == wanted) return entry.path().string();
    return {};
}

// The path the launcher tells the player to use for a game's mods.
inline std::string TitleFolderToCreate(const std::string& root, uint64_t title_id) {
    return root + "/" + TitleName(title_id);
}

// What a mod's folder holds, as Eden will use it.
inline unsigned Kinds(const fs::path& mod) {
    static constexpr std::string_view kCodeFiles[] = {"main", "main.npdm", "rtld", "sdk", "subsdk0", "subsdk1",
        "subsdk2", "subsdk3", "subsdk4", "subsdk5", "subsdk6", "subsdk7", "subsdk8", "subsdk9"};
    unsigned kinds = 0;
    for (const auto& part : ListFolder(mod)) {
        if (!IsFolder(part)) continue;
        const std::string name = Lower(part.path().filename().string());
        const auto inside = ListFolder(part.path());
        if (inside.empty()) continue;
        if (name == "romfs" || name == "romfslite" || name == "romfs_ext") kinds |= kFiles;
        if (name == "cheats") kinds |= kCheats;
        if (name != "exefs") continue;
        for (const auto& file : inside) {
            const std::string file_name = file.path().filename().string();
            const std::string extension = file.path().extension().string();
            if (extension == ".ips" || extension == ".pchtxt" ||
                std::find(std::begin(kCodeFiles), std::end(kCodeFiles), file_name) != std::end(kCodeFiles))
                kinds |= kCode;
        }
    }
    return kinds;
}

// The game's mods, in the order Eden applies them.
inline std::vector<Mod> List(const std::string& root, uint64_t title_id) {
    std::vector<Mod> mods;
    const std::string folder = TitleFolder(root, title_id);
    if (folder.empty()) return mods;
    for (const auto& entry : ListFolder(folder)) {
        const std::string name = entry.path().filename().string();
        if (IsFolder(entry)) {
            if (const unsigned kinds = Kinds(entry.path())) mods.push_back({name, kinds});
        } else if (name.starts_with("cheat_")) {
            mods.push_back({name, kCheats});
        }
    }
    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.name < b.name; });
    return mods;
}

// One line for the log and the crash report: the mods in use, by name.
inline std::string Summary(const std::vector<Mod>& mods, const std::vector<std::string>& disabled) {
    std::string used;
    unsigned off = 0;
    for (const Mod& mod : mods) {
        if (std::find(disabled.begin(), disabled.end(), mod.name) != disabled.end()) {
            ++off;
            continue;
        }
        used += (used.empty() ? "" : ", ") + mod.name;
    }
    if (used.empty()) used = "none";
    if (off != 0) used += " (" + std::to_string(off) + " switched off)";
    return used;
}
} // namespace Eden::Mods
