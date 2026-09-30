// SPDX-License-Identifier: GPL-3.0-or-later
// Everything the launcher remembers, in one JSON file (config/prosperoeden.json):
//
//   {
//     "version": 1,
//     "video": { "renderer": "vulkan", "fps_overlay": true },
//     "audio": { "volume": 100, "mute": false },
//     "diagnostics": { "detailed_logging": false },
//     "game_files": "/mnt/ext1/eden",
//     "library": { "last_game": "Game [id].nsp", "recent": ["Game [id].nsp"] },
//     "games": { "0100000000010000": { "console_mode": "handheld" } }
//   }
//
// Missing or mistyped values read as their defaults. Writes replace the file atomically. The
// text files of earlier versions (settings.txt, last-game.txt, recent-games.txt, assets-dir.txt,
// game-<title>-mode.txt) are read once into the JSON file and left in place.
#pragma once
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "storage_paths.h"

namespace Eden {
enum class GraphicsBackend { OpenGL, Vulkan };
inline const char* BackendName(GraphicsBackend backend) {
    return backend == GraphicsBackend::OpenGL ? "OpenGL" : "Vulkan";
}
struct Preferences {
    bool hud = true;
    int volume = 100;
    bool mute = false;
    bool detailed_logging = false;
    GraphicsBackend backend = GraphicsBackend::Vulkan;
};

inline bool ValidRomFilename(std::string_view name) {
    if (name.size() < 5 || name.size() > 255) return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || c == '/' || c == '\\') return false;
    std::string extension(name.substr(name.size() - 4));
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".nsp" || extension == ".xci";
}

inline std::string SettingsFile() { return ConfigFile("prosperoeden.json"); }

namespace Settings {
using Json = nlohmann::json;

inline std::string Folder(const std::string& file) {
    const auto slash = file.find_last_of('/');
    return slash == std::string::npos ? std::string{"."} : file.substr(0, slash);
}

// The whole file, or empty when it cannot be read completely.
inline bool ReadFile(const std::string& path, std::string& text) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    text.clear();
    char buffer[4096];
    for (std::size_t count; (count = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) text.append(buffer, count);
    const bool ok = !std::ferror(file);
    std::fclose(file);
    return ok;
}

inline bool WriteFile(const std::string& path, std::string_view contents) {
    const std::string temporary = path + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    bool ok = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    if (std::fflush(file) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (ok && std::rename(temporary.c_str(), path.c_str()) == 0) return true;
    std::remove(temporary.c_str());
    return false;
}

inline bool Write(const Json& document, const std::string& file) {
    return WriteFile(file, document.dump(2, ' ', false, Json::error_handler_t::replace) + "\n");
}

inline std::string TitleKey(uint64_t title_id) {
    char key[17];
    std::snprintf(key, sizeof(key), "%016" PRIX64, title_id);
    return key;
}

// The earlier text files, when they are still in the settings folder.
inline Json Legacy(const std::string& folder) {
    Json document = Json::object();
    std::string text;
    if (ReadFile(folder + "/settings.txt", text)) {
        int version, hud, volume, mute, logging, backend = 1;
        char extra;
        std::istringstream input(text);
        if ((input >> version >> hud >> volume >> mute >> logging) &&
            (version == 1 || (version == 2 && (input >> backend))) && !(input >> extra) &&
            (backend == 0 || backend == 1) && (hud == 0 || hud == 1) && volume >= 0 && volume <= 100 &&
            (mute == 0 || mute == 1) && (logging == 0 || logging == 1)) {
            document["video"] = {{"renderer", backend ? "vulkan" : "opengl"}, {"fps_overlay", hud != 0}};
            document["audio"] = {{"volume", volume}, {"mute", mute != 0}};
            document["diagnostics"] = {{"detailed_logging", logging != 0}};
        }
    }
    if (ReadFile(folder + "/last-game.txt", text) && ValidRomFilename(text))
        document["library"]["last_game"] = text;
    if (ReadFile(folder + "/recent-games.txt", text)) {
        Json recent = Json::array();
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line) && recent.size() < 4;)
            if (ValidRomFilename(line)) recent.push_back(line);
        if (!recent.empty()) document["library"]["recent"] = recent;
    }
    if (ReadFile(folder + "/assets-dir.txt", text)) {
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (ValidAssetsDir(text)) document["game_files"] = text;
    }
    return document;
}

// The settings document; the first read after an update builds it from the earlier text files.
inline Json Load(const std::string& file) {
    std::string text;
    if (ReadFile(file, text)) {
        Json document = Json::parse(text, nullptr, false);
        return document.is_object() ? document : Json::object();
    }
    Json document = Legacy(Folder(file));
    if (!document.empty()) {
        document["version"] = 1;
        (void)Write(document, file);
    }
    return document;
}

inline bool Bool(const Json& document, const Json::json_pointer& at, bool fallback) {
    return document.contains(at) && document.at(at).is_boolean() ? document.at(at).get<bool>() : fallback;
}
inline int Int(const Json& document, const Json::json_pointer& at, int fallback) {
    return document.contains(at) && document.at(at).is_number_integer() ? document.at(at).get<int>() : fallback;
}
inline std::string String(const Json& document, const Json::json_pointer& at) {
    return document.contains(at) && document.at(at).is_string() ? document.at(at).get<std::string>() : std::string{};
}
} // namespace Settings

inline Preferences LoadPreferences(const std::string& file = SettingsFile()) {
    using Settings::Json;
    const Json document = Settings::Load(file);
    Preferences result;
    result.hud = Settings::Bool(document, Json::json_pointer("/video/fps_overlay"), result.hud);
    result.backend = Settings::String(document, Json::json_pointer("/video/renderer")) == "opengl" ?
        GraphicsBackend::OpenGL : GraphicsBackend::Vulkan;
    const int volume = Settings::Int(document, Json::json_pointer("/audio/volume"), result.volume);
    if (volume >= 0 && volume <= 100) result.volume = volume;
    result.mute = Settings::Bool(document, Json::json_pointer("/audio/mute"), result.mute);
    result.detailed_logging = Settings::Bool(document, Json::json_pointer("/diagnostics/detailed_logging"),
                                             result.detailed_logging);
    return result;
}

inline bool SavePreferences(const Preferences& value, const std::string& file = SettingsFile()) {
    if (value.volume < 0 || value.volume > 100 ||
        (value.backend != GraphicsBackend::OpenGL && value.backend != GraphicsBackend::Vulkan)) return false;
    Settings::Json document = Settings::Load(file);
    document["version"] = 1;
    document["video"]["renderer"] = value.backend == GraphicsBackend::Vulkan ? "vulkan" : "opengl";
    document["video"]["fps_overlay"] = value.hud;
    document["audio"]["volume"] = value.volume;
    document["audio"]["mute"] = value.mute;
    document["diagnostics"]["detailed_logging"] = value.detailed_logging;
    return Settings::Write(document, file);
}

// Games run docked unless the player saved "Handheld" for that title in the launcher.
inline bool LoadGameDocked(uint64_t title_id, const std::string& file = SettingsFile()) {
    if (!title_id) return true;
    using Settings::Json;
    const Json document = Settings::Load(file);
    const Json::json_pointer at("/games/" + Settings::TitleKey(title_id) + "/console_mode");
    if (document.contains(at)) return Settings::String(document, at) != "handheld";
    // A mode saved by an earlier version, in its own file.
    char name[48];
    std::snprintf(name, sizeof(name), "/game-%016llx-mode.txt", static_cast<unsigned long long>(title_id));
    std::string text;
    return !(Settings::ReadFile(Settings::Folder(file) + name, text) && text == "1 handheld\n");
}

inline bool SaveGameDocked(uint64_t title_id, bool docked, const std::string& file = SettingsFile()) {
    if (!title_id) return false;
    Settings::Json document = Settings::Load(file);
    document["version"] = 1;
    document["games"][Settings::TitleKey(title_id)]["console_mode"] = docked ? "docked" : "handheld";
    return Settings::Write(document, file);
}

inline std::string LoadLastGame(const std::string& file = SettingsFile()) {
    const std::string name = Settings::String(Settings::Load(file), Settings::Json::json_pointer("/library/last_game"));
    return ValidRomFilename(name) ? name : std::string{};
}

inline bool SaveLastGame(std::string_view name, const std::string& file = SettingsFile()) {
    if (!ValidRomFilename(name)) return false;
    Settings::Json document = Settings::Load(file);
    document["version"] = 1;
    document["library"]["last_game"] = std::string(name);
    return Settings::Write(document, file);
}

inline std::vector<std::string> LoadRecentGames(const std::string& file = SettingsFile()) {
    using Settings::Json;
    const Json document = Settings::Load(file);
    const Json::json_pointer at("/library/recent");
    std::vector<std::string> recent;
    if (!document.contains(at) || !document.at(at).is_array()) return recent;
    for (const auto& entry : document.at(at)) {
        if (!entry.is_string()) continue;
        const std::string name = entry.get<std::string>();
        if (recent.size() < 4 && ValidRomFilename(name) && std::find(recent.begin(), recent.end(), name) == recent.end())
            recent.push_back(name);
    }
    return recent;
}

inline bool SaveRecentGame(std::string_view name, const std::string& file = SettingsFile()) {
    if (!ValidRomFilename(name)) return false;
    auto recent = LoadRecentGames(file);
    recent.erase(std::remove(recent.begin(), recent.end(), name), recent.end());
    recent.insert(recent.begin(), std::string(name));
    if (recent.size() > 4) recent.resize(4);
    Settings::Json document = Settings::Load(file);
    document["version"] = 1;
    document["library"]["recent"] = recent;
    return Settings::Write(document, file);
}

// The saved game files folder, or empty when none is saved.
inline std::string LoadSavedAssetsDir(const std::string& file = SettingsFile()) {
    const std::string value = Settings::String(Settings::Load(file), Settings::Json::json_pointer("/game_files"));
    return ValidAssetsDir(value) ? value : std::string{};
}

inline bool SaveAssetsDir(std::string_view directory, const std::string& file = SettingsFile()) {
    if (!ValidAssetsDir(directory)) return false;
    Settings::Json document = Settings::Load(file);
    document["version"] = 1;
    document["game_files"] = std::string(directory);
    return Settings::Write(document, file);
}

inline int AudioVolume(const Preferences& value) {
    return value.mute ? 0 : 0x8000 * value.volume / 100;
}
} // namespace Eden
