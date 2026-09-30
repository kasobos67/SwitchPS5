// SPDX-License-Identifier: GPL-3.0-or-later
// Everything the launcher remembers, in one JSON file (config/prosperoeden.json):
//
//   {
//     "version": 1,
//     "video": { "renderer": "vulkan", "fps_overlay": true, "resolution": "1x",
//                "upscaling_filter": "bilinear" },
//     "audio": { "volume": 100, "mute": false },
//     "controls": { "vibration": true },
//     "system": { "language": "en-US" },
//     "diagnostics": { "detailed_logging": false },
//     "game_files": "/mnt/ext1/eden",
//     "library": { "last_game": "Game [id].nsp", "recent": ["Game [id].nsp"] },
//     "games": { "0100000000010000": { "console_mode": "handheld", "renderer": "opengl",
//                                      "resolution": "0.75x", "upscaling_filter": "fsr" } }
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
// Settings > Video: the internal rendering resolution (a scale of the game's own 720p handheld
// or 1080p docked output) and the filter that scales the result to the TV output.
inline constexpr const char* kResolutionKeys[] = {"0.5x", "0.75x", "1x", "1.5x", "2x"};
inline constexpr const char* kResolutionLabels[] = {"0.5x (faster, softer)", "0.75x (faster)", "1x (native)",
                                                    "1.5x (sharper)", "2x (sharpest)"};
inline constexpr int kNativeResolution = 2;
inline constexpr const char* kUpscalingFilterKeys[] = {"bilinear", "fsr", "bicubic", "nearest"};
inline constexpr const char* kUpscalingFilterLabels[] = {"Bilinear", "AMD FSR", "Bicubic", "Nearest"};
// Settings > Language: the system language games see, in launcher order. Each entry maps to Eden's
// Settings::Language and to the Settings::Region consoles sold with that language have (indices in
// Eden's enum order; headless/main.cpp checks them). Eden's older "Chinese" and "Taiwanese" codes
// are left out: games use Chinese (Simplified) and Chinese (Traditional) instead.
inline constexpr const char* kLanguageKeys[] = {"en-US", "en-GB", "fr", "fr-CA", "de", "it", "es", "es-419", "pt",
                                                "pt-BR", "nl", "ru", "pl", "ja", "ko", "zh-Hans", "zh-Hant", "th"};
inline constexpr const char* kLanguageLabels[] = {"English (US)", "English (UK)", "French", "French (Canada)",
    "German", "Italian", "Spanish", "Spanish (Latin America)", "Portuguese", "Portuguese (Brazil)", "Dutch",
    "Russian", "Polish", "Japanese", "Korean", "Chinese (Simplified)", "Chinese (Traditional)", "Thai"};
inline constexpr int kLanguageSettings[] = {1, 12, 2, 13, 3, 4, 5, 14, 9, 17, 8, 10, 18, 0, 7, 15, 16, 19};
inline constexpr int kLanguageRegions[] = {1, 2, 2, 1, 2, 2, 2, 1, 2, 1, 2, 2, 2, 0, 5, 4, 6, 1};
static_assert(std::size(kLanguageLabels) == std::size(kLanguageKeys) &&
              std::size(kLanguageSettings) == std::size(kLanguageKeys) &&
              std::size(kLanguageRegions) == std::size(kLanguageKeys));
struct Preferences {
    bool hud = true;
    int volume = 100;
    bool mute = false;
    bool detailed_logging = false;
    GraphicsBackend backend = GraphicsBackend::Vulkan;
    int resolution = kNativeResolution;  // index into kResolutionKeys
    int upscaling_filter = 0;            // index into kUpscalingFilterKeys
    bool vibration = true;
    int language = 0;                    // index into kLanguageKeys (English (US), Eden's default)
};

inline int KeyIndex(const std::string& value, const char* const* keys, int count, int fallback) {
    for (int i = 0; i < count; ++i)
        if (value == keys[i]) return i;
    return fallback;
}

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
    result.resolution = KeyIndex(Settings::String(document, Json::json_pointer("/video/resolution")),
                                 kResolutionKeys, int(std::size(kResolutionKeys)), result.resolution);
    result.upscaling_filter = KeyIndex(Settings::String(document, Json::json_pointer("/video/upscaling_filter")),
                                       kUpscalingFilterKeys, int(std::size(kUpscalingFilterKeys)),
                                       result.upscaling_filter);
    result.vibration = Settings::Bool(document, Json::json_pointer("/controls/vibration"), result.vibration);
    result.language = KeyIndex(Settings::String(document, Json::json_pointer("/system/language")),
                               kLanguageKeys, int(std::size(kLanguageKeys)), result.language);
    return result;
}

inline bool SavePreferences(const Preferences& value, const std::string& file = SettingsFile()) {
    if (value.volume < 0 || value.volume > 100 ||
        (value.backend != GraphicsBackend::OpenGL && value.backend != GraphicsBackend::Vulkan) ||
        value.resolution < 0 || value.resolution >= int(std::size(kResolutionKeys)) ||
        value.upscaling_filter < 0 || value.upscaling_filter >= int(std::size(kUpscalingFilterKeys)) ||
        value.language < 0 || value.language >= int(std::size(kLanguageKeys))) return false;
    Settings::Json document = Settings::Load(file);
    document["version"] = 1;
    document["video"]["renderer"] = value.backend == GraphicsBackend::Vulkan ? "vulkan" : "opengl";
    document["video"]["fps_overlay"] = value.hud;
    document["video"]["resolution"] = kResolutionKeys[value.resolution];
    document["video"]["upscaling_filter"] = kUpscalingFilterKeys[value.upscaling_filter];
    document["audio"]["volume"] = value.volume;
    document["audio"]["mute"] = value.mute;
    document["controls"]["vibration"] = value.vibration;
    document["system"]["language"] = kLanguageKeys[value.language];
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

// Library > Game settings: renderer, resolution and upscaling filter for one game; -1 (absent
// from the file) uses Settings > Video.
struct GameSettings {
    int renderer = -1;          // 0 OpenGL, 1 Vulkan
    int resolution = -1;        // index into kResolutionKeys
    int upscaling_filter = -1;  // index into kUpscalingFilterKeys
};
inline constexpr const char* kRendererKeys[] = {"opengl", "vulkan"};

inline GameSettings LoadGameSettings(uint64_t title_id, const std::string& file = SettingsFile()) {
    GameSettings result;
    if (!title_id) return result;
    using Settings::Json;
    const Json document = Settings::Load(file);
    const std::string base = "/games/" + Settings::TitleKey(title_id);
    const auto key = [&](const char* name) { return Settings::String(document, Json::json_pointer(base + "/" + name)); };
    result.renderer = KeyIndex(key("renderer"), kRendererKeys, int(std::size(kRendererKeys)), -1);
    result.resolution = KeyIndex(key("resolution"), kResolutionKeys, int(std::size(kResolutionKeys)), -1);
    result.upscaling_filter = KeyIndex(key("upscaling_filter"), kUpscalingFilterKeys,
                                       int(std::size(kUpscalingFilterKeys)), -1);
    return result;
}

inline bool SaveGameSettings(uint64_t title_id, const GameSettings& value, const std::string& file = SettingsFile()) {
    if (!title_id || value.renderer >= int(std::size(kRendererKeys)) ||
        value.resolution >= int(std::size(kResolutionKeys)) ||
        value.upscaling_filter >= int(std::size(kUpscalingFilterKeys))) return false;
    Settings::Json document = Settings::Load(file);
    document["version"] = 1;
    auto& game = document["games"][Settings::TitleKey(title_id)];
    if (!game.is_object()) game = Settings::Json::object();
    const auto store = [&](const char* name, int index, const char* const* keys) {
        if (index < 0) game.erase(name);
        else game[name] = keys[index];
    };
    store("renderer", value.renderer, kRendererKeys);
    store("resolution", value.resolution, kResolutionKeys);
    store("upscaling_filter", value.upscaling_filter, kUpscalingFilterKeys);
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
