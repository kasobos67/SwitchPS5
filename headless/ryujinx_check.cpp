// SPDX-License-Identifier: GPL-3.0-or-later
// Host check for the Ryujinx save import (ryujinx_saves.h) on a generated Ryujinx data folder.
#include "ryujinx_saves.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace Eden::RyujinxSaves;

void require(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "Ryujinx save import FAIL: %s\n", what);
        std::exit(1);
    }
}

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// One IMEN entry as Ryujinx writes it: key (program ID, user ID, type) and value (save ID).
void add_entry(std::vector<char>& index, uint64_t program, uint64_t user_low, uint8_t type, uint64_t save_id) {
    std::vector<char> item(0x8C, 0);
    std::memcpy(item.data(), "IMEN", 4);
    const uint32_t size = 0x40;
    std::memcpy(item.data() + 4, &size, 4);
    std::memcpy(item.data() + 8, &size, 4);
    std::memcpy(item.data() + 0x0C, &program, 8);
    std::memcpy(item.data() + 0x14, &user_low, 8);
    item[0x2C] = static_cast<char>(type);
    std::memcpy(item.data() + 0x4C, &save_id, 8);
    index.insert(index.end(), item.begin(), item.end());
}
} // namespace

int main() {
    char pattern[] = "/tmp/ryujinx-check-XXXXXX";
    require(mkdtemp(pattern) != nullptr, "temporary folder");
    const fs::path base = pattern;
    const uint64_t game = 0x0100AAAA00000000ULL, other_game = 0x0100BBBB00000000ULL;

    // Ryujinx portable folder: game has an account save (first user), a second user's account save,
    // a device save and a cache save; other_game's only save folder is missing.
    const fs::path ryujinx = base / "ryujinx", root = ryujinx / "portable";
    std::vector<char> index(12, 0);
    std::memcpy(index.data(), "IMKV", 4);
    add_entry(index, game, 1, 1, 0x1);
    add_entry(index, game, 2, 1, 0x3);
    add_entry(index, game, 0, 3, 0x2);
    add_entry(index, game, 0, 5, 0x5);
    add_entry(index, other_game, 1, 1, 0x4);
    fs::create_directories(IndexPath(root).parent_path());
    std::ofstream(IndexPath(root), std::ios::binary).write(index.data(), static_cast<std::streamsize>(index.size()));
    const fs::path saves_root = root / "bis" / "user" / "save";
    write_text(saves_root / "0000000000000001" / "0" / "save.bin", "account-first-user");
    write_text(saves_root / "0000000000000001" / "0" / "sub" / "x.dat", "nested");
    write_text(saves_root / "0000000000000003" / "0" / "save.bin", "account-second-user");
    write_text(saves_root / "0000000000000002" / "0" / "device.bin", "device");
    write_text(saves_root / "0000000000000005" / "0" / "cache.bin", "cache");

    require(DataRoot(ryujinx) == root, "portable folder found");
    require(DataRoot(root) == root, "data folder itself found");
    std::string error;
    const auto saves = FindSaves(ryujinx, game, error);
    require(saves.size() == 2, "account and device saves only");
    require(saves[0].kind == Kind::Account && saves[0].folder == saves_root / "0000000000000001" / "0",
            "first user's account save");
    require(saves[1].kind == Kind::Device && saves[1].folder == saves_root / "0000000000000002" / "0", "device save");
    require(FindSaves(ryujinx, other_game, error).empty() && error == "none for this game", "missing save folder");
    require(FindSaves(base / "absent", game, error).empty() && error == "no Ryujinx data", "no data folder");

    // Import over an existing account save: it moves to the backup; the device folder is new.
    const fs::path users = base / "nand" / "user" / "save" / "0000000000000000";
    const fs::path account = users / "00112233445566778899AABBCCDDEEFF" / "0100AAAA00000000";
    const fs::path device = users / "00000000000000000000000000000000" / "0100AAAA00000000";
    const fs::path backups = base / "backup";
    write_text(account / "old.bin", "previous");
    require(Import(saves, account, device, backups, "0100AAAA00000000-1", error), "import");
    require(read_text(account / "save.bin") == "account-first-user", "account save copied");
    require(read_text(account / "sub" / "x.dat") == "nested", "nested file copied");
    require(!fs::exists(account / "old.bin"), "old save replaced");
    require(read_text(device / "device.bin") == "device", "device save copied");
    require(read_text(backups / "0100AAAA00000000-1-account" / "old.bin") == "previous", "old save kept");
    require(!fs::exists(backups / "0100AAAA00000000-1-device"), "no device backup without a device save");

    // A failed copy leaves the current save in place and no partial copy behind.
    const std::vector<Save> broken{{Kind::Account, base / "absent" / "0"}};
    require(!Import(broken, account, device, backups, "0100AAAA00000000-2", error), "failed copy reported");
    require(read_text(account / "save.bin") == "account-first-user", "current save restored");
    require(!fs::exists(backups / "0100AAAA00000000-2-account"), "backup moved back");

    // A damaged index (a cut-off entry) is reported, not read past; an empty one has no saves.
    std::ofstream(IndexPath(root), std::ios::binary) << "IMKV----------IMEN-cut";
    require(FindSaves(ryujinx, game, error).empty() && error == "save index unreadable", "damaged index");
    std::ofstream(IndexPath(root), std::ios::binary) << "IMKV--------";
    require(FindSaves(ryujinx, game, error).empty() && error == "none for this game", "empty index");

    fs::remove_all(base);
    std::printf("Ryujinx save import PASS: portable folder, account/device choice, backup, restore on failure\n");
    return 0;
}
