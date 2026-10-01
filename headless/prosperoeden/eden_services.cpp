// SPDX-License-Identifier: GPL-3.0-or-later
#include "eden_services.h"

#include "assets_dir.h"
#include "diagnostics.h"
#include "metadata_bridge.h"
#include "native_directory.h"
#include "version.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <initializer_list>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool IsFile(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

std::string AddOnSummary(uint64_t title_id) {
    char update[64]{};
    unsigned dlc = 0;
    eden_game_addons(title_id, update, sizeof(update), &dlc);
    std::string text = update[0] ? std::string("Update ") + update : std::string{};
    if (dlc) text += (text.empty() ? "" : ", ") + std::to_string(dlc) + " DLC";
    return text.empty() ? "None" : text;
}

// The language a game will use for the chosen one (Settings > Language), and a note when the game
// does not offer the choice and falls back to another language.
struct GameLanguage {
    std::string label;
    std::string note;
};
GameLanguage LanguageFor(const std::string& path, uint64_t title_id, int choice) {
    const int chosen = Eden::kLanguageSettings[choice];
    const int used = eden_game_language(path.c_str(), Eden::AssetsPath("keys").c_str(), title_id, chosen);
    GameLanguage result{Eden::kLanguageLabels[choice], {}};
    if (used == chosen) return result;
    result.label = "Another language";
    for (std::size_t i = 0; i < std::size(Eden::kLanguageSettings); ++i)
        if (Eden::kLanguageSettings[i] == used) result.label = Eden::kLanguageLabels[i];
    result.note = std::string(Eden::kLanguageLabels[choice]) + " not available";
    return result;
}

// Names of the subfolders (folders = true) or regular files in path, sorted without regard
// to case. Unlike ReadNativeDirectory, an odd entry is skipped rather than failing the
// folder: the Game files browser walks the whole console filesystem.
std::vector<std::string> ListEntries(const std::string& path, bool folders, bool& ok) {
    ok = false;
    std::vector<std::string> names;
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return names;
    std::vector<char> buffer(65536);
    for (;;) {
        const int count = sceKernelGetdents(fd, buffer.data(), static_cast<int>(buffer.size()));
        if (count == 0) { ok = true; break; }
        if (count < 0 || count > static_cast<int>(buffer.size())) break;
        for (std::size_t offset = 0; offset + offsetof(dirent, d_name) < static_cast<std::size_t>(count);) {
            uint16_t length;
            uint8_t type;
            std::memcpy(&length, buffer.data() + offset + offsetof(dirent, d_reclen), sizeof(length));
            std::memcpy(&type, buffer.data() + offset + offsetof(dirent, d_type), sizeof(type));
            if (length <= offsetof(dirent, d_name) || offset + length > static_cast<std::size_t>(count)) break;
            const char* name = buffer.data() + offset + offsetof(dirent, d_name);
            const std::string entry(name, strnlen(name, length - offsetof(dirent, d_name)));
            offset += length;
            if (entry.empty() || entry == "." || entry == "..") continue;
            bool is_folder = type == DT_DIR, is_file = type == DT_REG;
            if (type == DT_UNKNOWN || type == DT_LNK) {
                struct stat info {};
                const std::string full = path == "/" ? "/" + entry : path + "/" + entry;
                if (stat(full.c_str(), &info) != 0) continue;
                is_folder = S_ISDIR(info.st_mode);
                is_file = S_ISREG(info.st_mode);
            }
            if (folders ? is_folder : is_file) names.push_back(entry);
        }
    }
    close(fd);
    const auto lower = [](std::string text) {
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
        return lower(a) < lower(b);
    });
    return names;
}

std::string JoinPath(const std::string& directory, const std::string& name) {
    return directory == "/" ? "/" + name : directory + "/" + name;
}

// Files in directory with one of the (lower-case) extensions; -1 when it cannot be read.
int CountFiles(const std::string& directory, std::initializer_list<const char*> extensions) {
    bool ok = false;
    int count = 0;
    for (const auto& name : ListEntries(directory, false, ok)) {
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* extension : extensions)
            if (lower.size() > std::strlen(extension) && lower.ends_with(extension)) { ++count; break; }
    }
    return ok ? count : -1;
}

unsigned int HashPath(const std::string& path) {
    unsigned int hash = 2166136261u;
    for (const unsigned char byte : path) hash = (hash ^ byte) * 16777619u;
    return hash;
}

// A title from the file's name: without its extension and the tags dumps carry
// ("Name [0100...][v0]", "Name (USA)").
std::string CleanTitle(const std::string& filename) {
    std::string title = std::filesystem::path(filename).stem().string();
    if (title.rfind("[Game] ", 0) == 0) title.erase(0, 7);
    if (const auto tag = title.find_first_of("[("); tag != std::string::npos && tag > 0) title.erase(tag);
    while (!title.empty() && (title.back() == ' ' || title.back() == '_' || title.back() == '-')) title.pop_back();
    return title.empty() ? std::filesystem::path(filename).stem().string() : title;
}

// Covers are keyed by the ROM's file name, not its full path, so they survive a new game
// files folder.
std::string CoverPath(const std::string& filename) {
    char name[16]{};
    std::snprintf(name, sizeof(name), "/%08x.tga", HashPath(filename));
    return Eden::CoversDir() + name;
}

// The game's own name is kept beside its cover once the ROM has been read, so the home screen
// can name its games without opening them.
std::string NamePath(const std::string& filename) {
    char name[16]{};
    std::snprintf(name, sizeof(name), "/%08x.name", HashPath(filename));
    return Eden::CoversDir() + name;
}

std::string SavedTitle(const std::string& filename) {
    char text[513]{};
    if (std::FILE* file = std::fopen(NamePath(filename).c_str(), "rb")) {
        const std::size_t size = std::fread(text, 1, sizeof(text) - 1, file);
        std::fclose(file);
        text[size] = '\0';
    }
    return text;
}

void SaveTitle(const std::string& filename, const std::string& title) {
    if (title.empty() || SavedTitle(filename) == title) return;
    const std::string path = NamePath(filename);
    const std::string staged = path + ".new";
    std::FILE* file = std::fopen(staged.c_str(), "wb");
    if (!file) return;
    const bool written = std::fwrite(title.data(), 1, title.size(), file) == title.size();
    if (std::fclose(file) != 0 || !written || std::rename(staged.c_str(), path.c_str()) != 0)
        (void)std::remove(staged.c_str());
}

std::string GameTitle(const std::string& filename) {
    const std::string saved = SavedTitle(filename);
    return saved.empty() ? CleanTitle(filename) : saved;
}

// The cached cover of a ROM, extracted from it when missing; empty when it has none.
std::string EnsureCover(const std::string& filename, std::string* title = nullptr) {
    const std::string cover = CoverPath(filename);
    if (Eden::FileExists(cover) && !title) return cover;
    const std::string rom = Eden::AssetsPath("roms/" + filename);
    if (!Eden::FileExists(rom)) return {};
    (void)mkdir(Eden::CoversDir().c_str(), 0777);
    char extracted[513]{};
    const int metadata = eden_extract_game_metadata(rom.c_str(), Eden::AssetsPath("keys").c_str(), cover.c_str(),
                                                    extracted, sizeof(extracted));
    if (metadata & EDEN_METADATA_TITLE) {
        SaveTitle(filename, extracted);
        if (title) *title = extracted;
    }
    if (metadata & EDEN_METADATA_COVER) return cover;
    Eden::Report("cover", ("No cover extracted from " + filename).c_str());
    return Eden::FileExists(cover) ? cover : std::string{};
}

int CountInstalledGames() {
    std::error_code error;
    const auto entries = Eden::ReadNativeDirectory(Eden::AssetsPath("roms"), error);
    if (error) return 0;
    int count = 0;
    for (const auto& entry : entries) {
        const std::string filename = entry.path().filename().string();
        if (Eden::ValidRomFilename(filename) && IsFile(Eden::AssetsPath("roms/" + filename))) ++count;
    }
    return count;
}

template <std::size_t N>
std::vector<std::string> Labels(const char* const (&values)[N]) {
    return std::vector<std::string>(std::begin(values), std::end(values));
}

} // namespace

EdenServices::EdenServices(std::string launch_error) : launch_error_(std::move(launch_error)) {
    (void)mkdir(Eden::ConfigDir().c_str(), 0777);
    setup_ = eden_startup_error();
    Eden::Report("setup", setup_.empty() ? "Keys and firmware startup checks passed" : setup_.c_str());
}

pe::ui::Home EdenServices::home() {
    const std::lock_guard lock(bridge_);
    pe::ui::Home home;
    home.setup_ready = setup_.empty();
    if (!home.setup_ready) {
        home.status = "Setup required: " + setup_ +
            " Open Settings, Game files to choose the folder that holds your keys, firmware and roms folders"
            " (or add the files to " + Eden::AssetsDir() + "), then reopen ProsperoEden.";
    } else if (!launch_error_.empty()) {
        home.status = "Game could not start: " + launch_error_ + " Details: " + Eden::LogFile("stderr.log");
        home.launch_failed = true;
    }

    home.last_file = Eden::LoadLastGame();
    const std::string last_path = Eden::AssetsPath("roms/" + home.last_file);
    home.last_exists = !home.last_file.empty() && IsFile(last_path);
    if (!home.last_file.empty()) {
        std::string title = GameTitle(home.last_file);
        std::string cover = CoverPath(home.last_file);
        bool has_cover = Eden::FileExists(cover);
        if (home.last_exists && home.setup_ready && !has_cover) {
            cover = EnsureCover(home.last_file, &title);
            has_cover = !cover.empty();
        }
        home.last_title = title;
        home.last_caption = "ROM missing from the game files folder";
        if (home.last_exists) {
            home.last_caption = "Last game opened";
            struct stat history_info {};
            if (stat(Eden::ConfigFile("last-game.txt").c_str(), &history_info) == 0) {
                char when[64]{};
                if (const std::tm* local = std::localtime(&history_info.st_mtime))
                    if (std::strftime(when, sizeof(when), "Last launched %b %d at %H:%M", local))
                        home.last_caption = when;
            }
        }
        if (has_cover) home.last_cover = cover;
    }
    // The last game's update and DLC, and the language it will use (a warning when it does not
    // offer the chosen one).
    if (home.setup_ready && home.last_exists) {
        eden_scan_addons(Eden::AssetsPath("updates").c_str(), Eden::AssetsPath("keys").c_str());
        const uint64_t title_id = eden_game_title_id(last_path.c_str());
        const GameLanguage language = LanguageFor(last_path, title_id, Eden::LoadPreferences().language);
        home.last_info = "Add-ons: " + AddOnSummary(title_id) + "  /  Language: " + language.label +
            (language.note.empty() ? "" : " (" + language.note + " in this game)");
        home.last_info_warning = !language.note.empty();
    }

    auto history = Eden::LoadRecentGames();
    if (history.empty() && home.last_exists) {
        if (!Eden::SaveRecentGame(home.last_file))
            Eden::Report("history", "Could not seed recent games from last played game");
        history.push_back(home.last_file);
    }
    for (const auto& name : history) {
        if (!IsFile(Eden::AssetsPath("roms/" + name))) continue;
        home.recents.push_back({name, GameTitle(name), EnsureCover(name)});
    }
    const int installed = CountInstalledGames();
    home.system_status = std::to_string(installed) + (installed == 1 ? " game" : " games") +
        " installed  /  " + (home.setup_ready ? "Firmware ready" : "Setup required");
    return home;
}

std::string EdenServices::clock() {
    const std::time_t now = std::time(nullptr);
    char label[32]{};
    if (const std::tm* local = std::localtime(&now))
        (void)std::strftime(label, sizeof(label), "%H:%M", local);
    return label;
}

std::string EdenServices::version() {
    // "01.000.040" reads as v1.000.040.
    const char* text = Eden::kAppVersion;
    while (text[0] == '0' && text[1] != '.' && text[1] != '\0') ++text;
    return std::string("v") + text;
}

std::vector<pe::ui::Game> EdenServices::games() {
    // The launcher reads the list beside its menu (pe/ui/library.cpp), so the metadata reader
    // is used by one thread at a time.
    const std::lock_guard lock(bridge_);
    std::vector<pe::ui::Game> games;
    (void)mkdir(Eden::ConfigDir().c_str(), 0777);
    (void)mkdir(Eden::CoversDir().c_str(), 0777);
    std::error_code directory_error;
    const auto entries = Eden::ReadNativeDirectory(Eden::AssetsPath("roms"), directory_error);
    if (directory_error) return games;
    eden_scan_addons(Eden::AssetsPath("updates").c_str(), Eden::AssetsPath("keys").c_str());
    const int language_choice = Eden::LoadPreferences().language;
    for (const auto& entry : entries) {
        const std::string file = entry.path().filename().string();
        const std::size_t dot = file.find_last_of('.');
        if (file == "." || file == ".." || dot == std::string::npos) continue;
        std::string format = file.substr(dot + 1);
        std::transform(format.begin(), format.end(), format.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (format != "NSP" && format != "XCI") continue;
        const std::string path = Eden::AssetsPath("roms/" + file);
        struct stat info {};
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
        char size[32];
        const double bytes = static_cast<double>(info.st_size);
        if (bytes >= 1073741824.0) std::snprintf(size, sizeof(size), "%.1f GB", bytes / 1073741824.0);
        else std::snprintf(size, sizeof(size), "%.1f MB", bytes / 1048576.0);
        pe::ui::Game game;
        game.name = CleanTitle(file);
        game.format = format;
        game.size = size;
        game.file = file;
        // A game whose data cannot be read is still listed, by its file name.
        try {
            char title[513]{};
            // The cover is replaced in one step: the menu may be loading the old one right now.
            const std::string cover = CoverPath(file);
            const std::string staged = cover + ".new";
            const int metadata = eden_extract_game_metadata(path.c_str(), Eden::AssetsPath("keys").c_str(),
                                                            staged.c_str(), title, sizeof(title));
            if (metadata & EDEN_METADATA_TITLE) {
                game.name = title;
                SaveTitle(file, game.name);
            }
            if ((metadata & EDEN_METADATA_COVER) && std::rename(staged.c_str(), cover.c_str()) == 0)
                game.cover = cover;
            else
                (void)std::remove(staged.c_str());
            game.title_id = eden_game_title_id(path.c_str());
            const GameLanguage language = LanguageFor(path, game.title_id, language_choice);
            game.addons = AddOnSummary(game.title_id);
            game.language = language.label;
            game.language_note = language.note;
        } catch (const std::exception& error) {
            Eden::Report("library", (file + ": " + error.what()).c_str());
        }
        games.push_back(std::move(game));
    }
    std::sort(games.begin(), games.end(),
              [](const pe::ui::Game& a, const pe::ui::Game& b) { return a.name < b.name; });
    return games;
}

std::string EdenServices::game_path(const std::string& file) { return Eden::AssetsPath("roms/" + file); }

bool EdenServices::docked(std::uint64_t title_id) { return Eden::LoadGameDocked(title_id); }

bool EdenServices::set_docked(std::uint64_t title_id, bool docked) {
    return Eden::SaveGameDocked(title_id, docked);
}

pe::ui::GameSettings EdenServices::game_settings(std::uint64_t title_id) {
    const Eden::GameSettings saved = Eden::LoadGameSettings(title_id);
    return {saved.renderer, saved.resolution, saved.upscaling_filter};
}

bool EdenServices::set_game_settings(std::uint64_t title_id, const pe::ui::GameSettings& settings) {
    const bool saved = Eden::SaveGameSettings(title_id, {settings.renderer, settings.resolution, settings.filter});
    if (!saved) Eden::Report("settings", "Could not write game settings");
    return saved;
}

pe::ui::Preferences EdenServices::preferences() {
    const Eden::Preferences saved = Eden::LoadPreferences();
    pe::ui::Preferences result;
    result.hud = saved.hud;
    result.volume = saved.volume;
    result.mute = saved.mute;
    result.detailed_logging = saved.detailed_logging;
    result.renderer = saved.backend == Eden::GraphicsBackend::OpenGL ? 0 : 1;
    result.resolution = saved.resolution;
    result.filter = saved.upscaling_filter;
    result.vibration = saved.vibration;
    result.language = saved.language;
    result.menu_volume = saved.menu_volume;
    return result;
}

bool EdenServices::set_preferences(const pe::ui::Preferences& preferences) {
    Eden::Preferences value;
    value.hud = preferences.hud;
    value.volume = preferences.volume;
    value.mute = preferences.mute;
    value.detailed_logging = preferences.detailed_logging;
    value.backend = preferences.renderer == 0 ? Eden::GraphicsBackend::OpenGL : Eden::GraphicsBackend::Vulkan;
    value.resolution = preferences.resolution;
    value.upscaling_filter = preferences.filter;
    value.vibration = preferences.vibration;
    value.language = preferences.language;
    value.menu_volume = preferences.menu_volume;
    const bool saved = Eden::SavePreferences(value);
    if (!saved) Eden::Report("settings", "Could not write preferences");
    return saved;
}

const std::vector<std::string>& EdenServices::resolution_labels() {
    static const std::vector<std::string> labels = Labels(Eden::kResolutionLabels);
    return labels;
}

const std::vector<std::string>& EdenServices::resolution_keys() {
    static const std::vector<std::string> labels = Labels(Eden::kResolutionKeys);
    return labels;
}

const std::vector<std::string>& EdenServices::filter_labels() {
    static const std::vector<std::string> labels = Labels(Eden::kUpscalingFilterLabels);
    return labels;
}

const std::vector<std::string>& EdenServices::language_labels() {
    static const std::vector<std::string> labels = Labels(Eden::kLanguageLabels);
    return labels;
}

std::string EdenServices::language_region(int language) {
    static constexpr const char* kRegions[] = {"Japan", "USA", "Europe", "Australia", "China", "Korea", "Taiwan"};
    if (language < 0 || language >= int(std::size(Eden::kLanguageRegions))) return {};
    return kRegions[Eden::kLanguageRegions[language]];
}

std::string EdenServices::setup_details() {
    return setup_.empty() ?
        "Keys and firmware: startup checks passed. Game-specific compatibility is checked at launch." : setup_;
}

bool EdenServices::folders(const std::string& directory, std::vector<std::string>* names) {
    bool ok = false;
    *names = ListEntries(directory, true, ok);
    return ok;
}

pe::ui::FolderInfo EdenServices::folder_info(const std::string& directory) {
    pe::ui::FolderInfo info;
    info.keys = Eden::FileExists(JoinPath(directory, "keys/prod.keys"));
    info.firmware = CountFiles(JoinPath(directory, "firmware"), {".nca"});
    info.games = CountFiles(JoinPath(directory, "roms"), {".nsp", ".xci"});
    return info;
}

std::string EdenServices::files_folder() { return Eden::AssetsDir(); }
std::string EdenServices::saved_files_folder() { return Eden::LoadSavedAssetsDir(); }
std::string EdenServices::default_files_folder() { return Eden::kDefaultAssetsDir; }

bool EdenServices::set_files_folder(const std::string& directory) {
    const bool saved = Eden::SaveAssetsDir(directory);
    if (!saved) Eden::Report("settings", "Could not write the game files folder");
    return saved;
}

int EdenServices::filesystem_access() { return Eden::FilesystemAccessStatus(); }

#ifdef EDEN_SAVE_IMPORT
// Ryujinx save import (ryujinx_saves.h): a Ryujinx data folder copied into ryujinx/ next to roms/.
bool EdenServices::save_import_available() { return true; }

bool EdenServices::save_import_status(std::uint64_t title_id, std::string* text) {
    char status[96]{};
    const bool found = eden_ryujinx_save_status(title_id, status, sizeof(status)) != 0;
    *text = status;
    return found;
}

bool EdenServices::save_import(std::uint64_t title_id, std::string* message) {
    char text[192]{};
    const bool imported = eden_ryujinx_import_save(title_id, text, sizeof(text)) != 0;
    *message = text;
    return imported;
}
#else
bool EdenServices::save_import_available() { return false; }
bool EdenServices::save_import_status(std::uint64_t, std::string*) { return false; }
bool EdenServices::save_import(std::uint64_t, std::string*) { return false; }
#endif

bool EdenServices::load_image(const std::string& path, pe::gfx::Image* image) {
    // Covers have full paths; the launcher's own art is named from its ui folder.
    return pe::gfx::load_tga(!path.empty() && path[0] == '/' ? path : Eden::AppFile("ui/" + path), image);
}
