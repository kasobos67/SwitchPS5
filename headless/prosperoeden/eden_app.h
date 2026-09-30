#pragma once

#include "radio_input.h"
#include "preferences.h"
#include <string>
#include <vector>
#include <ctime>

namespace Rml { class ElementDocument; }

class EdenApp final {
public:
    bool Initialize(Rml::ElementDocument* document, const std::string& launch_error = {});
    void HandleInput(const radio_input_event_t& event);
    void Poll();
    void Shutdown() { document_ = nullptr; }
    const std::string& SelectedGame() const { return selected_game_; }

private:
    void Update();
    void UpdateSettings();
    void SaveSettings();
    void UpdateDialog();
    void Open(const char* id, int dialog);
    void Close();
    // Settings > Game files: a folder browser for the keys/firmware/roms folder.
    void OpenFiles();
    bool BrowseTo(const std::string& directory);
    void HandleFilesInput(const radio_input_event_t& event);
    void UpdateFiles();
    // Library > Game settings for the selected game.
    void OpenGameSettings();
    void HandleGameSettingsInput(const radio_input_event_t& event);
    void UpdateGameSettings(const char* message = nullptr);

    Rml::ElementDocument* document_ = nullptr;
    Eden::Preferences preferences_;
    int option_ = 0;
    std::string last_game_;
    std::vector<std::string> recent_games_;
    std::time_t shown_minute_ = 0;
    bool continue_ready_ = false;
    bool setup_ready_ = false;
    int selected_ = 0;
    int dialog_ = 0;
    int dialog_selected_ = 0;
    std::string selected_game_;
    std::string browse_dir_;
    std::vector<std::string> browse_entries_; // ".." first unless at "/", then subfolders
    int browse_selected_ = 0;
    std::string files_message_;
};
