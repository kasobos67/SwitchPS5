// Host check of the JSON settings store (settings_store.h) and its migration from the earlier
// text files. Build: c++ -std=c++20 -I<nlohmann include> preferences_check.cpp && ./a.out
#include "preferences.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <unistd.h>

static std::string Read(const std::string& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}

int main() {
    char directory[] = "/tmp/eden-settings-XXXXXX";
    assert(mkdtemp(directory));
    const std::string file = std::string(directory) + "/prosperoeden.json";

    // Defaults with no file.
    assert(Eden::LoadPreferences(file).volume == 100);
    assert(Eden::LoadPreferences(file).backend == Eden::GraphicsBackend::Vulkan);
    assert(Eden::LoadPreferences(file).hud);

    // Preferences round trip and validation.
    assert(Eden::SavePreferences({false, 40, true, true, Eden::GraphicsBackend::OpenGL}, file));
    auto saved = Eden::LoadPreferences(file);
    assert(!saved.hud && saved.volume == 40 && saved.mute && saved.detailed_logging &&
           saved.backend == Eden::GraphicsBackend::OpenGL);
    assert(!Eden::SavePreferences({true, 101, false, false}, file));
    assert(Eden::LoadPreferences(file).volume == 40);
    assert(Read(file).find("\"renderer\": \"opengl\"") != std::string::npos);

    // Last and recent games.
    assert(Eden::SaveLastGame("Sample Quest [id].nsp", file));
    assert(Eden::LoadLastGame(file) == "Sample Quest [id].nsp");
    for (const char* invalid : {"../escape.nsp", "game.zip", "a/b.nsp"}) assert(!Eden::SaveLastGame(invalid, file));
    assert(Eden::LoadLastGame(file) == "Sample Quest [id].nsp");
    assert(Eden::SaveRecentGame("Sample Quest.nsp", file));
    assert(Eden::SaveRecentGame("Demo Racer.xci", file));
    assert(Eden::SaveRecentGame("Sample Quest.nsp", file));
    assert((Eden::LoadRecentGames(file) == std::vector<std::string>{"Sample Quest.nsp", "Demo Racer.xci"}));
    for (int i = 0; i < 5; ++i) assert(Eden::SaveRecentGame("Game" + std::to_string(i) + ".nsp", file));
    assert((Eden::LoadRecentGames(file) == std::vector<std::string>{"Game4.nsp", "Game3.nsp", "Game2.nsp", "Game1.nsp"}));

    // Console mode per title.
    constexpr uint64_t racer = 0x0100000000010000, quest = 0x0100000000030000;
    assert(Eden::LoadGameDocked(racer, file) && Eden::LoadGameDocked(quest, file));
    assert(Eden::SaveGameDocked(racer, false, file));
    assert(!Eden::LoadGameDocked(racer, file) && Eden::LoadGameDocked(quest, file));
    assert(!Eden::SaveGameDocked(0, true, file));
    assert(Read(file).find("\"0100000000010000\"") != std::string::npos);

    // Game files folder.
    assert(Eden::LoadSavedAssetsDir(file).empty());
    assert(Eden::SaveAssetsDir("/mnt/ext1/eden", file));
    assert(Eden::LoadSavedAssetsDir(file) == "/mnt/ext1/eden");
    assert(!Eden::SaveAssetsDir("relative/path", file) && !Eden::SaveAssetsDir("/a/../b", file));

    // Everything saved so far survives in one document.
    assert(Eden::LoadPreferences(file).volume == 40 && Eden::LoadLastGame(file) == "Sample Quest [id].nsp");

    // A damaged file reads as defaults and is replaced by the next save.
    { std::ofstream out(file); out << "{ not json"; }
    assert(Eden::LoadPreferences(file).volume == 100 && Eden::LoadLastGame(file).empty());
    assert(Eden::SaveLastGame("Game.XCI", file) && Eden::LoadLastGame(file) == "Game.XCI");

    // Migration from the text files of earlier versions, once, leaving them in place.
    char legacy[] = "/tmp/eden-legacy-XXXXXX";
    assert(mkdtemp(legacy));
    const std::string folder = legacy, migrated = folder + "/prosperoeden.json";
    { std::ofstream(folder + "/settings.txt") << "2 0 70 0 1 0\n"; }
    { std::ofstream(folder + "/last-game.txt") << "Demo Racer [0100000000010000].nsp"; }
    { std::ofstream(folder + "/recent-games.txt") << "Demo Racer [0100000000010000].nsp\nTest Platformer.nsp\n"; }
    { std::ofstream(folder + "/assets-dir.txt") << "/mnt/ext1/eden\n"; }
    { std::ofstream(folder + "/game-0100000000030000-mode.txt") << "1 handheld\n"; }
    const auto old = Eden::LoadPreferences(migrated);
    assert(!old.hud && old.volume == 70 && !old.mute && old.detailed_logging &&
           old.backend == Eden::GraphicsBackend::OpenGL);
    assert(std::filesystem::exists(migrated));
    assert(Eden::LoadLastGame(migrated) == "Demo Racer [0100000000010000].nsp");
    assert((Eden::LoadRecentGames(migrated) == std::vector<std::string>{"Demo Racer [0100000000010000].nsp", "Test Platformer.nsp"}));
    assert(Eden::LoadSavedAssetsDir(migrated) == "/mnt/ext1/eden");
    assert(!Eden::LoadGameDocked(quest, migrated) && Eden::LoadGameDocked(racer, migrated));
    assert(std::filesystem::exists(folder + "/settings.txt"));

    std::filesystem::remove_all(directory);
    std::filesystem::remove_all(legacy);
    std::puts("settings store: all checks passed");
}
