#pragma once

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace pe::ui
{
// Launcher-only preferences. This deliberately does not change emulator configuration.
struct Avatar { int icon = 0; int color = 0; };
struct PresentationState
{
    struct UserAvatar { std::string name; Avatar value; };
    bool white_theme = false;
    std::vector<std::string> recent_games;
    std::vector<UserAvatar> avatars;
    Avatar avatar(const std::string &name) const
    {
        for (const auto &entry : avatars) if (entry.name == name) return entry.value;
        return {};
    }
    void set_avatar(const std::string &name, int icon, int color)
    {
        Avatar value{std::clamp(icon, 0, 146), std::clamp(color, 0, 71)};
        for (auto &entry : avatars) if (entry.name == name) { entry.value = value; return; }
        if (avatars.size() < 64) avatars.push_back({name, value});
    }
    void remember(const std::string &file)
    {
        recent_games.erase(std::remove(recent_games.begin(), recent_games.end(), file), recent_games.end());
        recent_games.insert(recent_games.begin(), file);
        if (recent_games.size() > 7) recent_games.resize(7);
    }
    bool load(const std::string &file)
    {
        std::ifstream in(file);
        std::string magic;
        if (!std::getline(in, magic) || magic != "SwitchUIPresentation1") return false;
        PresentationState next;
        std::string key;
        while (in >> key)
        {
            if (key == "white") { int value; if (!(in >> value) || value < 0 || value > 1) return false; next.white_theme = value != 0; }
            else if (key == "recent") { std::string value; if (!(in >> std::quoted(value)) || value.size() > 4096 || next.recent_games.size() >= 7) return false; next.recent_games.push_back(value); }
            else if (key == "avatar") { std::string name; int icon, color; if (!(in >> std::quoted(name) >> icon >> color) || name.size() > 1024 || icon < 0 || icon > 146 || color < 0 || color > 71 || next.avatars.size() >= 64) return false; next.set_avatar(name, icon, color); }
            else return false;
        }
        if (!in.eof()) return false;
        *this = std::move(next);
        return true;
    }
    bool save(const std::string &file) const
    {
        const auto temporary = file + ".tmp";
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << "SwitchUIPresentation1\nwhite " << (white_theme ? 1 : 0) << '\n';
        for (const auto &entry : recent_games) out << "recent " << std::quoted(entry) << '\n';
        for (const auto &entry : avatars) out << "avatar " << std::quoted(entry.name) << ' ' << entry.value.icon << ' ' << entry.value.color << '\n';
        out.flush();
        const bool written = out.good();
        out.close();
        if (!written || std::rename(temporary.c_str(), file.c_str()) != 0) { std::remove(temporary.c_str()); return false; }
        return true;
    }
};
}
