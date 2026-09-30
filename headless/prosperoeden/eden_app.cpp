#include "eden_app.h"
#include "assets_dir.h"
#include "diagnostics.h"
#include "metadata_bridge.h"
#include "native_directory.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>



namespace {
// Reports a launcher input handler slower than 50 ms (the Library was reported slow to browse).
struct InputTiming {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int dialog;
    int key;
    ~InputTiming() {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (ms >= 50)
            Eden::Report("slow input", ("dialog " + std::to_string(dialog) + " key " + std::to_string(key) +
                                        ": " + std::to_string(ms) + " ms").c_str());
    }
};

void SetClass(Rml::ElementDocument* document, const char* id, const char* name, bool enabled) {
    if (Rml::Element* element = document->GetElementById(id)) element->SetClass(name, enabled);
}

void SetText(Rml::ElementDocument* document, const char* id, const char* text) {
    if (Rml::Element* element = document->GetElementById(id))
        element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
}

struct GameInfo {
    std::string name;
    std::string format;
    std::string size;
    std::string path;
    std::string cover;
    uint64_t title_id = 0;
};

std::vector<GameInfo> games;

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

std::string ParentPath(const std::string& directory) {
    const std::size_t slash = directory.find_last_of('/');
    return slash == 0 || slash == std::string::npos ? "/" : directory.substr(0, slash);
}

// A path that fits a label: the end is what tells folders apart.
std::string ShortPath(const std::string& path, std::size_t limit) {
    return path.size() <= limit ? path : "..." + path.substr(path.size() - (limit - 3));
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

std::string GameTitle(const std::string& filename) {
    std::string title = std::filesystem::path(filename).stem().string();
    if (title.rfind("[Game] ", 0) == 0) title.erase(0, 7);
    if (const auto id = title.find(" ["); id != std::string::npos) title.erase(id);
    return title;
}

// Covers are keyed by the ROM's file name, not its full path, so they survive a new game
// files folder.
std::string CoverPath(const std::string& filename) {
    char name[16]{};
    std::snprintf(name, sizeof(name), "/%08x.tga", HashPath(filename));
    return Eden::CoversDir() + name;
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
    if (title && (metadata & EDEN_METADATA_TITLE)) *title = extracted;
    if (metadata & EDEN_METADATA_COVER) return cover;
    Eden::Report("cover", ("No cover extracted from " + filename).c_str());
    return Eden::FileExists(cover) ? cover : std::string{};
}

std::string CachedCover(const std::string& filename) {
    const std::string cover = EnsureCover(filename);
    return cover.empty() ? "icons/prosperoeden.tga" : cover;
}

int CountInstalledGames() {
    std::error_code error;
    const auto entries = Eden::ReadNativeDirectory(Eden::AssetsPath("roms"), error);
    if (error) return 0;
    int count = 0;
    for (const auto& entry : entries) {
        const std::string filename = entry.path().filename().string();
        if (!Eden::ValidRomFilename(filename)) continue;
        struct stat info {};
        const std::string path = Eden::AssetsPath("roms/" + filename);
        if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode)) ++count;
    }
    return count;
}

void LoadGames() {
    games.clear();
    (void)mkdir(Eden::ConfigDir().c_str(), 0777);
    (void)mkdir(Eden::CoversDir().c_str(), 0777);
    std::error_code directory_error;
    const auto entries = Eden::ReadNativeDirectory(Eden::AssetsPath("roms"), directory_error);
    if (directory_error) return;
    for (const auto& entry : entries) {
        const std::string file = entry.path().filename().string();
        const std::size_t dot = file.find_last_of('.');
        if (file == "." || file == ".." || dot == std::string::npos) continue;
        std::string format = file.substr(dot + 1);
        std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (format != "NSP" && format != "XCI") continue;
        const std::string path = Eden::AssetsPath("roms/" + file);
        struct stat info {};
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
        char size[32];
        const double bytes = static_cast<double>(info.st_size);
        if (bytes >= 1073741824.0) std::snprintf(size, sizeof(size), "%.1f GB", bytes / 1073741824.0);
        else std::snprintf(size, sizeof(size), "%.1f MB", bytes / 1048576.0);
        char title[513]{};
        const std::string cover_path = CoverPath(file);
        const char* cover = cover_path.c_str();
        const int metadata = eden_extract_game_metadata(path.c_str(), Eden::AssetsPath("keys").c_str(), cover,
                                                        title, sizeof(title));
        games.push_back({metadata & EDEN_METADATA_TITLE ? title : file.substr(0, dot), format,
                         size, file, metadata & EDEN_METADATA_COVER ? cover : "",
                         eden_game_title_id(path.c_str())});
    }
    std::sort(games.begin(), games.end(), [](const GameInfo& a, const GameInfo& b) { return a.name < b.name; });
}
}

bool EdenApp::Initialize(Rml::ElementDocument* document, const std::string& launch_error) {
    document_ = document;
    if (!document_) return false;
    (void)mkdir(Eden::ConfigDir().c_str(), 0777);
    preferences_ = Eden::LoadPreferences();
    const std::string setup = eden_startup_error();
    setup_ready_ = setup.empty();
    Eden::Report("setup", setup_ready_ ? "Keys and firmware startup checks passed" : setup.c_str());
    const std::string message = !setup_ready_ ? "Setup required: " + setup +
        " Open Settings, Game files to choose the folder that holds your keys, firmware and roms folders"
        " (or add the files to " + Eden::AssetsDir() + "), then reopen ProsperoEden." :
        launch_error.empty() ? "Setup checks passed. Game-specific keys and compatibility are checked at launch." :
        "Game could not start: " + launch_error +
        " Details: " + Eden::LogFile("stderr.log");
    SetText(document_, "startup-status", message.c_str());
    SetClass(document_, "startup-status", "quiet", setup_ready_ && launch_error.empty());
    SetClass(document_, "recent-section", "library-hidden", !setup_ready_ || !launch_error.empty());
    SetClass(document_, "load-rom", "disabled", !setup_ready_);
    if (!setup_ready_) {
        document_->GetElementById("load-rom")->SetAttribute("disabled", "disabled");
        selected_ = 2;
    }
    last_game_ = Eden::LoadLastGame();
    struct stat info {};
    const std::string last_path = Eden::AssetsPath("roms/" + last_game_);
    const bool last_exists = !last_game_.empty() &&
        stat(last_path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    continue_ready_ = setup_ready_ && last_exists;
    if (!last_game_.empty()) {
        std::string title = GameTitle(last_game_);
        std::string cover = CoverPath(last_game_);
        bool has_cover = Eden::FileExists(cover);
        if (last_exists && setup_ready_ && !has_cover) {
            cover = EnsureCover(last_game_, &title);
            has_cover = !cover.empty();
        }
        SetText(document_, "last-played-title", title.c_str());
        std::string caption = "ROM missing from the game files folder";
        if (last_exists) {
            caption = "Last game opened";
            struct stat history_info {};
            if (stat(Eden::ConfigFile("last-game.txt").c_str(), &history_info) == 0) {
                char when[64]{};
                if (const std::tm* local = std::localtime(&history_info.st_mtime))
                    if (std::strftime(when, sizeof(when), "Last launched %b %d at %H:%M", local))
                        caption = when;
            }
        }
        SetText(document_, "last-played-caption", caption.c_str());
        if (has_cover) document_->GetElementById("last-played-cover")->SetAttribute("src", cover.c_str());
    }
    SetClass(document_, "continue-game", "disabled", !setup_ready_);
    SetClass(document_, "hero-options", "disabled", !last_exists || !setup_ready_);
    SetText(document_, "continue-copy", continue_ready_ ? "Launch game" : "Open library");
    auto history = Eden::LoadRecentGames();
    if (history.empty() && last_exists) {
        if (!Eden::SaveRecentGame(last_game_))
            Eden::Report("history", "Could not seed recent games from last played game");
        history.push_back(last_game_);
    }
    for (const auto& name : history) {
        const std::string path = Eden::AssetsPath("roms/" + name);
        struct stat game_info {};
        if (stat(path.c_str(), &game_info) == 0 && S_ISREG(game_info.st_mode))
            recent_games_.push_back(name);
    }
    for (int i = 0; i < 4; ++i) {
        const std::string row = "recent-" + std::to_string(i);
        const bool present = i < static_cast<int>(recent_games_.size());
        SetClass(document_, row.c_str(), "library-hidden", !present);
        if (!present) continue;
        const std::string title = GameTitle(recent_games_[i]);
        const std::string title_id = "recent-title-" + std::to_string(i);
        const std::string cover_id = "recent-cover-" + std::to_string(i);
        SetText(document_, title_id.c_str(), title.c_str());
        document_->GetElementById(cover_id)->SetAttribute("src", CachedCover(recent_games_[i]));
    }
    SetClass(document_, "recent-empty", "library-hidden", !recent_games_.empty());
    // About: where this process reads the player's files (Settings > Game files).
    SetText(document_, "about-keys-path", ShortPath(Eden::AssetsPath("keys/prod.keys"), 36).c_str());
    SetText(document_, "about-firmware-path", ShortPath(Eden::AssetsPath("firmware/*.nca"), 36).c_str());
    SetText(document_, "about-games-path", (ShortPath(Eden::AssetsPath("roms"), 36) + "/ (NSP or XCI)").c_str());
    const int installed = CountInstalledGames();
    const std::string system = std::to_string(installed) + (installed == 1 ? " game" : " games") +
        " installed  /  " + (setup_ready_ ? "Firmware ready" : "Setup required");
    SetText(document_, "system-status", system.c_str());
    Poll();
    selected_ = continue_ready_ ? 0 : setup_ready_ ? 1 : 2;
    Update();
    return document_ != nullptr;
}

void EdenApp::HandleInput(const radio_input_event_t& event) {
    if (!event.pressed || !document_) return;
    const InputTiming timing{.dialog = dialog_, .key = static_cast<int>(event.key)};
    if (dialog_ == 7) {
        if (event.key == RADIO_INPUT_CIRCLE) Close();
        return;
    }
    if (dialog_ == 8) {
        HandleFilesInput(event);
        return;
    }
    if (dialog_ >= 3) {
        auto* select = static_cast<Rml::ElementFormControlSelect*>(document_->GetElementById("video-backend"));
        if (dialog_ == 3 && select->IsSelectBoxVisible()) {
            if (event.key == RADIO_INPUT_CIRCLE) {
                select->CancelSelectBox();
                UpdateSettings();
            } else if (event.key == RADIO_INPUT_UP || event.key == RADIO_INPUT_DOWN) {
                select->SetSelection(1 - select->GetSelection());
            } else if (event.key == RADIO_INPUT_CROSS) {
                preferences_.backend = select->GetSelection() == 0 ?
                    Eden::GraphicsBackend::OpenGL : Eden::GraphicsBackend::Vulkan;
                select->HideSelectBox();
                SaveSettings();
            }
            return;
        }
        if (event.key == RADIO_INPUT_CIRCLE) {
            static constexpr const char* pages[] = {"video-dialog", "audio-dialog", "controls-dialog", "diagnostics-dialog"};
            SetClass(document_, pages[dialog_ - 3], "open", false);
            dialog_ = 2;
            UpdateDialog();
            return;
        }
        if ((dialog_ == 3 || dialog_ == 4) &&
            (event.key == RADIO_INPUT_UP || event.key == RADIO_INPUT_DOWN)) {
            option_ = 1 - option_;
            UpdateSettings();
            return;
        }
        const bool activate = event.key == RADIO_INPUT_CROSS;
        const bool adjust = event.key == RADIO_INPUT_LEFT || event.key == RADIO_INPUT_RIGHT;
        if (dialog_ == 3 && option_ == 0 && activate) {
            select->Focus(); select->ShowSelectBox();
        } else if (dialog_ == 3 && option_ == 1 && (activate || adjust)) {
            preferences_.hud = !preferences_.hud; SaveSettings();
        } else if (dialog_ == 4 && option_ == 0 && adjust) {
            preferences_.volume = std::clamp(preferences_.volume +
                (event.key == RADIO_INPUT_RIGHT ? 10 : -10), 0, 100);
            SaveSettings();
        } else if (dialog_ == 4 && option_ == 1 && (activate || adjust)) {
            preferences_.mute = !preferences_.mute; SaveSettings();
        } else if (dialog_ == 6 && (activate || adjust)) {
            preferences_.detailed_logging = !preferences_.detailed_logging; SaveSettings();
        }
        return;
    }
    if (dialog_) {
        const int count = dialog_ == 1 ? static_cast<int>(games.size()) : 5;
        if (event.key == RADIO_INPUT_CIRCLE) Close();
        else if (dialog_ == 2 && event.key == RADIO_INPUT_CROSS && dialog_selected_ == 4) OpenFiles();
        else if (dialog_ == 2 && event.key == RADIO_INPUT_CROSS) {
            static constexpr const char* pages[] = {"video-dialog", "audio-dialog", "controls-dialog", "diagnostics-dialog"};
            SetClass(document_, pages[dialog_selected_], "open", true);
            dialog_ = 3 + dialog_selected_;
            option_ = 0;
            UpdateSettings();
        }
        else if (dialog_ == 1 && count > 0 &&
                 (event.key == RADIO_INPUT_LEFT || event.key == RADIO_INPUT_RIGHT)) {
            const auto id = games[dialog_selected_].title_id;
            if (!id) return;
            const bool saved = Eden::SaveGameDocked(id, !Eden::LoadGameDocked(id));
            UpdateDialog();
            SetText(document_, "game-mode-hint-text", saved ? "Saved for this game. Applies on next launch." :
                    "Could not save console mode. Please try again.");
        }
        else if (dialog_ == 1 && setup_ready_ && count > 0 && event.key == RADIO_INPUT_CROSS)
            selected_game_ = Eden::AssetsPath("roms/" + games[dialog_selected_].path);
        else if (count > 0 && event.key == RADIO_INPUT_UP) {
            dialog_selected_ = (dialog_selected_ + count - 1) % count;
            UpdateDialog();
        } else if (count > 0 && event.key == RADIO_INPUT_DOWN) {
            dialog_selected_ = (dialog_selected_ + 1) % count;
            UpdateDialog();
        }
        return;
    }
    const int recent_count = static_cast<int>(recent_games_.size());
    if (event.key == RADIO_INPUT_UP) {
        if (selected_ == 0 || selected_ == 4) selected_ = setup_ready_ ? 1 : 2;
        else if (selected_ >= 5) selected_ = 0;
    } else if (event.key == RADIO_INPUT_DOWN) {
        if (selected_ >= 1 && selected_ <= 3) {
            if (setup_ready_) selected_ = 0;
        } else if (selected_ == 0 || selected_ == 4) selected_ = recent_count ? 5 : 9;
    } else if (event.key == RADIO_INPUT_LEFT || event.key == RADIO_INPUT_RIGHT) {
        const int delta = event.key == RADIO_INPUT_RIGHT ? 1 : -1;
        if (selected_ >= 1 && selected_ <= 3) {
            selected_ = 1 + (selected_ - 1 + delta + 3) % 3;
            if (selected_ == 1 && !setup_ready_) selected_ = delta > 0 ? 2 : 3;
        } else if (selected_ == 0 || selected_ == 4) {
            if (continue_ready_) selected_ = selected_ == 0 ? 4 : 0;
        } else if (selected_ >= 5) {
            const int length = recent_count + 1;
            int position = selected_ == 9 ? recent_count : selected_ - 5;
            position = (position + delta + length) % length;
            selected_ = position == recent_count ? 9 : position + 5;
        }
    } else if (event.key == RADIO_INPUT_TRIANGLE && (selected_ == 0 || selected_ == 4) && continue_ready_) {
        Open("rom-dialog", 1);
        for (int i = 0; i < static_cast<int>(games.size()); ++i)
            if (games[i].path == last_game_) { dialog_selected_ = i; break; }
        UpdateDialog();
    } else if (event.key == RADIO_INPUT_CROSS) {
        if (selected_ == 0 && continue_ready_) selected_game_ = Eden::AssetsPath("roms/" + last_game_);
        else if ((selected_ == 0 || selected_ == 1 || selected_ == 9) && setup_ready_) Open("rom-dialog", 1);
        else if (selected_ == 2) Open("settings-dialog", 2);
        else if (selected_ == 3) Open("about-dialog", 7);
        else if (selected_ == 4 && continue_ready_) {
            Open("rom-dialog", 1);
            for (int i = 0; i < static_cast<int>(games.size()); ++i)
                if (games[i].path == last_game_) { dialog_selected_ = i; break; }
            UpdateDialog();
        } else if (selected_ >= 5 && selected_ < 5 + recent_count && setup_ready_)
            selected_game_ = Eden::AssetsPath("roms/" + recent_games_[selected_ - 5]);
    }
    Update();
}

void EdenApp::Update() {
    SetClass(document_, "continue-game", "focused", selected_ == 0);
    SetClass(document_, "load-rom", "focused", selected_ == 1);
    SetClass(document_, "settings", "focused", selected_ == 2);
    SetClass(document_, "help-about", "focused", selected_ == 3);
    SetClass(document_, "hero-options", "focused", selected_ == 4);
    SetClass(document_, "view-all", "focused", selected_ == 9);
    for (int i = 0; i < 4; ++i) {
        const std::string row = "recent-" + std::to_string(i);
        SetClass(document_, row.c_str(), "focused", selected_ == 5 + i);
    }
}

void EdenApp::Poll() {
    if (!document_) return;
    const std::time_t minute = std::time(nullptr) / 60;
    if (minute == shown_minute_) return;
    shown_minute_ = minute;
    const std::time_t now = minute * 60;
    char label[32]{};
    if (const std::tm* local = std::localtime(&now))
        (void)std::strftime(label, sizeof(label), "%H:%M", local);
    SetText(document_, "menu-clock", label);
}

void EdenApp::UpdateDialog() {
    static constexpr const char* rom_rows[] = {"rom-row-0", "rom-row-1", "rom-row-2",
        "rom-row-3", "rom-row-4", "rom-row-5", "rom-row-6"};
    static constexpr const char* rom_names[] = {"rom-name-0", "rom-name-1", "rom-name-2",
        "rom-name-3", "rom-name-4", "rom-name-5", "rom-name-6"};
    static constexpr const char* rom_formats[] = {"rom-format-0", "rom-format-1", "rom-format-2",
        "rom-format-3", "rom-format-4", "rom-format-5", "rom-format-6"};
    static constexpr const char* settings_rows[] = {"settings-row-0", "settings-row-1", "settings-row-2",
        "settings-row-3", "settings-row-4"};
    for (int row = 0; row < 5; ++row)
        SetClass(document_, settings_rows[row], "focused", dialog_ == 2 && dialog_selected_ == row);

    if (dialog_ == 1) {
        const int scroll = dialog_selected_ < 7 ? 0 : dialog_selected_ - 6;
        const int count = static_cast<int>(games.size());
        const auto title_id = count ? games[dialog_selected_].title_id : 0;
        SetClass(document_, "game-mode-setting", "setting-disabled", !title_id);
        const bool docked = title_id && Eden::LoadGameDocked(title_id);
        SetClass(document_, "game-mode-value", "unavailable", !title_id);
        SetClass(document_, "mode-docked", "active", title_id && docked);
        SetClass(document_, "mode-handheld", "active", title_id && !docked);
        SetText(document_, "game-mode-hint-text", title_id ?
                "Change mode. Saved per game." : "Select a readable game to configure its mode.");
        for (int row = 0; row < 7; ++row) {
            const int game_index = scroll + row;
            SetClass(document_, rom_rows[row], "focused", game_index == dialog_selected_);
            SetClass(document_, rom_rows[row], "offscreen", game_index >= count);
            SetText(document_, rom_names[row], game_index < count ? games[game_index].name.c_str() : "");
            SetText(document_, rom_formats[row], game_index < count ? games[game_index].format.c_str() : "");
        }
        SetClass(document_, "library-empty", "visible", count == 0);
        SetClass(document_, "rom-scrollbar", "offscreen", count <= 7);
        if (count == 0) {
            SetText(document_, "game-detail-title", "No ROM selected");
            SetText(document_, "game-detail-format", "-");
            SetText(document_, "game-detail-size", "-");
            SetText(document_, "game-detail-path", "-");
            if (Rml::Element* cover = document_->GetElementById("game-cover"))
                cover->SetAttribute("src", "icons/prosperoeden.tga");
            SetText(document_, "cover-caption", "Select a game");
        } else {
            const GameInfo& game = games[dialog_selected_];
            SetText(document_, "game-detail-title", game.name.c_str());
            SetText(document_, "game-detail-format", game.format.c_str());
            SetText(document_, "game-detail-size", game.size.c_str());
            SetText(document_, "game-detail-path", game.path.c_str());
            if (Rml::Element* cover = document_->GetElementById("game-cover"))
                cover->SetAttribute("src", game.cover.empty() ? "icons/prosperoeden.tga" : game.cover);
            SetText(document_, "cover-caption", game.cover.empty() ? "No cover art" : "");
        }
        char position[24];
        std::snprintf(position, sizeof(position), "%d OF %d", count ? dialog_selected_ + 1 : 0, count);
        SetText(document_, "library-position", position);
        if (count > 7) if (Rml::Element* thumb = document_->GetElementById("rom-scrollbar-thumb")) {
            char top[24];
            std::snprintf(top, sizeof(top), "%dpx", 496 * scroll / (count - 7));
            thumb->SetProperty("top", top);
        }
    }
}

void EdenApp::Open(const char* id, int dialog) {
    const bool full_screen = dialog == 1 || dialog == 2 || dialog == 7;
    for (const char* element : {"header", "menu", "last-played-card", "recent-section", "startup-status", "footer"})
        SetClass(document_, element, "library-hidden", full_screen);
    SetClass(document_, id, "open", true);
    dialog_ = dialog;
    if (dialog == 1) LoadGames();
    dialog_selected_ = 0;
    UpdateDialog();
}

void EdenApp::Close() {
    SetClass(document_, "about-dialog", "open", false);
    SetClass(document_, "rom-dialog", "open", false);
    SetClass(document_, "settings-dialog", "open", false);
    SetClass(document_, "files-dialog", "open", false);
    for (const char* element : {"header", "menu", "last-played-card", "recent-section", "startup-status", "footer"})
        SetClass(document_, element, "library-hidden", false);
    dialog_ = 0;
}

void EdenApp::UpdateSettings() {
    auto* backend = static_cast<Rml::ElementFormControlSelect*>(document_->GetElementById("video-backend"));
    backend->SetSelection(preferences_.backend == Eden::GraphicsBackend::OpenGL ? 0 : 1);
    SetText(document_, "hud-label", preferences_.hud ? "FPS overlay: On" : "FPS overlay: Off");
    const std::string volume = "Game volume: " + std::to_string(preferences_.volume) + "%";
    SetText(document_, "volume-label", volume.c_str());
    SetText(document_, "mute-label", preferences_.mute ? "Mute: On" : "Mute: Off");
    SetText(document_, "logging-label", preferences_.detailed_logging ? "Detailed logging: On" : "Detailed logging: Off");
    const std::string setup = eden_startup_error();
    SetText(document_, "setup-details", setup.empty() ?
        "Keys and firmware: startup checks passed. Game-specific compatibility is checked at launch." : setup.c_str());
    SetClass(document_, "hud-setting", "focused", option_ == 1);
    SetClass(document_, "volume-setting", "focused", option_ == 0);
    SetClass(document_, "mute-setting", "focused", option_ == 1);
    SetClass(document_, "video-backend-chrome", "dimmed", option_ != 0);
}

void EdenApp::SaveSettings() {
    const bool saved = Eden::SavePreferences(preferences_);
    static constexpr const char* hints[] = {"video-hint", "audio-hint", "controls-hint", "diagnostics-hint"};
    SetText(document_, hints[dialog_ - 3], saved ? "Saved. Applies when a game starts. O Back" : "Could not save settings. Please try again.");
    if (!saved) Eden::Report("settings", "Could not write preferences");
    UpdateSettings();
}

void EdenApp::OpenFiles() {
    // Start where the player's files are (or will be after reopening), else the nearest
    // folder that can be opened.
    std::string start = Eden::LoadSavedAssetsDir();
    if (start.empty()) start = Eden::AssetsDir();
    while (!BrowseTo(start) && start != "/") start = ParentPath(start);
    files_message_.clear();
    SetClass(document_, "files-dialog", "open", true);
    dialog_ = 8;
    UpdateFiles();
}

bool EdenApp::BrowseTo(const std::string& directory) {
    bool ok = false;
    auto folders = ListEntries(directory, true, ok);
    if (!ok) return false;
    browse_dir_ = directory;
    browse_entries_.clear();
    if (directory != "/") browse_entries_.push_back("..");
    browse_entries_.insert(browse_entries_.end(), folders.begin(), folders.end());
    browse_selected_ = 0;
    return true;
}

void EdenApp::HandleFilesInput(const radio_input_event_t& event) {
    const int count = static_cast<int>(browse_entries_.size());
    if (event.key == RADIO_INPUT_CIRCLE) {
        SetClass(document_, "files-dialog", "open", false);
        dialog_ = 2;
        UpdateDialog();
        return;
    }
    if (event.key == RADIO_INPUT_UP && count) {
        browse_selected_ = (browse_selected_ + count - 1) % count;
    } else if (event.key == RADIO_INPUT_DOWN && count) {
        browse_selected_ = (browse_selected_ + 1) % count;
    } else if ((event.key == RADIO_INPUT_L1 || event.key == RADIO_INPUT_R1) && count) {
        // A page (the six visible rows) at a time, stopping at the ends.
        browse_selected_ = std::clamp(browse_selected_ + (event.key == RADIO_INPUT_R1 ? 6 : -6), 0, count - 1);
    } else if (event.key == RADIO_INPUT_CROSS && count) {
        const std::string entry = browse_entries_[browse_selected_];
        const std::string from = browse_dir_;
        const bool up = entry == "..";
        if (!BrowseTo(up ? ParentPath(browse_dir_) : JoinPath(browse_dir_, entry))) {
            files_message_ = "This folder cannot be opened.";
        } else {
            files_message_.clear();
            // Going up keeps the folder just left in view.
            if (up) {
                const std::string left = from.substr(from.find_last_of('/') + 1);
                for (int i = 0; i < static_cast<int>(browse_entries_.size()); ++i)
                    if (browse_entries_[i] == left) { browse_selected_ = i; break; }
            }
        }
    } else if (event.key == RADIO_INPUT_TRIANGLE) {
        const bool saved = Eden::SaveAssetsDir(browse_dir_);
        if (!saved) Eden::Report("settings", "Could not write the game files folder");
        files_message_ = saved ? "Saved. Reopen ProsperoEden to use this folder." :
            "Could not save the folder. Please try again.";
    } else if (event.key == RADIO_INPUT_SQUARE) {
        const bool saved = Eden::SaveAssetsDir(Eden::kDefaultAssetsDir);
        if (!saved) Eden::Report("settings", "Could not write the game files folder");
        (void)BrowseTo(Eden::kDefaultAssetsDir);
        files_message_ = saved ? "Default folder saved. Reopen ProsperoEden to use it." :
            "Could not save the folder. Please try again.";
    } else {
        return;
    }
    UpdateFiles();
}

void EdenApp::UpdateFiles() {
    static constexpr int kRows = 6;
    const int count = static_cast<int>(browse_entries_.size());
    const int scroll = browse_selected_ < kRows ? 0 : browse_selected_ - (kRows - 1);
    for (int row = 0; row < kRows; ++row) {
        const int index = scroll + row;
        const bool up = index < count && browse_entries_[index] == "..";
        const std::string id = "files-row-" + std::to_string(row);
        const std::string name = "files-name-" + std::to_string(row);
        const std::string meta = "files-meta-" + std::to_string(row);
        SetClass(document_, id.c_str(), "focused", index == browse_selected_);
        SetClass(document_, id.c_str(), "offscreen", index >= count);
        SetClass(document_, id.c_str(), "files-up", up);
        SetText(document_, name.c_str(), index >= count ? "" : up ? "Parent folder" : browse_entries_[index].c_str());
        SetText(document_, meta.c_str(), index >= count ? "" : up ? "UP" : "OPEN");
    }
    SetClass(document_, "files-empty", "visible", count == 0);
    SetClass(document_, "files-scrollbar", "offscreen", count <= kRows);
    if (count > kRows) if (Rml::Element* thumb = document_->GetElementById("files-scrollbar-thumb")) {
        char top[24];
        std::snprintf(top, sizeof(top), "%dpx", 408 * scroll / (count - kRows));
        thumb->SetProperty("top", top);
    }
    SetText(document_, "files-path", ShortPath(browse_dir_, 52).c_str());
    char position[24];
    std::snprintf(position, sizeof(position), "%d OF %d", count ? browse_selected_ + 1 : 0, count);
    SetText(document_, "files-position", position);

    // The folder shown is what TRIANGLE saves: show what it holds.
    SetText(document_, "files-current", ShortPath(browse_dir_, 64).c_str());
    const bool keys = Eden::FileExists(JoinPath(browse_dir_, "keys/prod.keys"));
    const int firmware = CountFiles(JoinPath(browse_dir_, "firmware"), {".nca"});
    const int roms = CountFiles(JoinPath(browse_dir_, "roms"), {".nsp", ".xci"});
    const auto plural = [](int n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    };
    SetText(document_, "files-keys", keys ? "prod.keys found" : "prod.keys missing");
    SetText(document_, "files-firmware", firmware < 0 ? "No firmware folder" :
            plural(firmware, "NCA file", "NCA files").c_str());
    SetText(document_, "files-games", roms < 0 ? "No roms folder" : plural(roms, "game", "games").c_str());
    SetClass(document_, "files-keys", "ready", keys);
    SetClass(document_, "files-firmware", "ready", firmware > 0);
    SetClass(document_, "files-games", "ready", roms > 0);

    const std::string saved = Eden::LoadSavedAssetsDir();
    std::string in_use = ShortPath(Eden::AssetsDir(), 40);
    if (!saved.empty() && saved != Eden::AssetsDir()) in_use = "Next launch: " + ShortPath(saved, 32);
    SetText(document_, "files-in-use", in_use.c_str());
    const int access = Eden::FilesystemAccessStatus();
    const std::string access_text = access == 0 ? "Full filesystem" :
        "Sandboxed (code " + std::to_string(access) + "): app folder only";
    SetText(document_, "files-access", access_text.c_str());
    SetClass(document_, "files-access", "ready", access == 0);
    SetText(document_, "files-message", files_message_.empty() ?
            "Keep keys, firmware and roms folders together. TRIANGLE uses the folder shown." :
            files_message_.c_str());
}
