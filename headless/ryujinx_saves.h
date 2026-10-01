// SPDX-License-Identifier: GPL-3.0-or-later
// Ryujinx save import. A Ryujinx data folder (the one holding bis/, or a portable folder around it)
// keeps each save in bis/user/save/<save ID, 16 lowercase hex digits>/0 and lists the saves in its
// save index, bis/system/save/8000000000000000/0/imkvdb.arc: a 12-byte "IMKV" header, then one
// 0x8C-byte "IMEN" entry per save, whose key holds the program ID (entry offset 0x0C) and the save
// type (0x2C: 1 account, 3 device) and whose value starts with the save ID (0x4C). Eden's desktop
// app links such folders (common/fs/ryujinx_compat.cpp reads the same index but drops the type);
// the PS5 has no Ryujinx beside ProsperoEden, so the launcher copies a game's saves in instead.
#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

#if defined(__PROSPERO__)
#include "native_directory.h"
#endif

namespace Eden::RyujinxSaves {
namespace fs = std::filesystem;

enum class Kind : uint8_t { Account = 1, Device = 3 };

struct Save {
    Kind kind;
    fs::path folder;  // bis/user/save/<save ID>/0
};

inline bool ReadFile(const fs::path& path, std::vector<char>& data) {
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    data.clear();
    char buffer[16384];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) data.insert(data.end(), buffer, buffer + count);
    close(fd);
    return count == 0;
}

inline fs::path IndexPath(const fs::path& root) {
    return root / "bis" / "system" / "save" / "8000000000000000" / "0" / "imkvdb.arc";
}

// The Ryujinx data root in folder: folder itself or its portable/ subfolder, whichever holds the
// save index; empty when neither does.
inline fs::path DataRoot(const fs::path& folder) {
    for (const fs::path& root : {folder, folder / "portable"}) {
        std::error_code error;
        if (fs::is_regular_file(IndexPath(root), error)) return root;
    }
    return {};
}

// The game's saves in the Ryujinx data folder: the first account save (Ryujinx's first user with
// one) and the device save, each when its folder exists. Empty with error set when there is none.
inline std::vector<Save> FindSaves(const fs::path& folder, uint64_t title_id, std::string& error) {
    std::vector<Save> saves;
    const fs::path root = DataRoot(folder);
    if (root.empty()) {
        error = "no Ryujinx data";
        return saves;
    }
    std::vector<char> index;
    constexpr std::size_t header = 12, entry = 0x8C;
    if (!ReadFile(IndexPath(root), index) || index.size() < header || std::memcmp(index.data(), "IMKV", 4) != 0 ||
        (index.size() - header) % entry != 0) {
        error = "save index unreadable";
        return saves;
    }
    for (std::size_t offset = header; offset < index.size(); offset += entry) {
        const char* item = index.data() + offset;
        if (std::memcmp(item, "IMEN", 4) != 0) {
            error = "save index unreadable";
            return {};
        }
        uint64_t program = 0, save_id = 0;
        std::memcpy(&program, item + 0x0C, sizeof(program));
        std::memcpy(&save_id, item + 0x4C, sizeof(save_id));
        const auto kind = static_cast<Kind>(static_cast<uint8_t>(item[0x2C]));
        if (program != title_id || (kind != Kind::Account && kind != Kind::Device)) continue;
        bool taken = false;
        for (const Save& save : saves) taken |= save.kind == kind;
        if (taken) continue;
        char name[17];
        std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(save_id));
        const fs::path save_folder = root / "bis" / "user" / "save" / name / "0";
        std::error_code status_error;
        if (fs::is_directory(save_folder, status_error)) saves.push_back({kind, save_folder});
    }
    if (saves.empty()) error = "none for this game";
    return saves;
}

inline std::vector<fs::directory_entry> ListFolder(const fs::path& folder, std::error_code& error) {
#if defined(__PROSPERO__)
    // Mounted PS5 drives need larger directory reads than the C library's iterator makes.
    return Eden::ReadNativeDirectory(folder, error);
#else
    std::vector<fs::directory_entry> entries;
    for (fs::directory_iterator it{folder, error}, end; !error && it != end; it.increment(error))
        entries.push_back(*it);
    return entries;
#endif
}

inline bool CopyFile(const fs::path& from, const fs::path& to) {
    const int in = open(from.c_str(), O_RDONLY);
    if (in < 0) return false;
    const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        close(in);
        return false;
    }
    std::vector<char> buffer(1 << 20);
    bool ok = true;
    for (;;) {
        const ssize_t count = read(in, buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) {
            ok = false;
            break;
        }
        ssize_t done = 0;
        while (done < count) {
            const ssize_t written = write(out, buffer.data() + done, static_cast<std::size_t>(count - done));
            if (written <= 0) break;
            done += written;
        }
        if (done < count) {
            ok = false;
            break;
        }
    }
    close(in);
    return close(out) == 0 && ok;
}

inline bool CopyTree(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::create_directories(to, error);
    if (error) return false;
    const auto entries = ListFolder(from, error);
    if (error) return false;
    for (const auto& entry : entries) {
        const fs::path target = to / entry.path().filename();
        std::error_code type_error;
        const bool copied = entry.is_directory(type_error) ? CopyTree(entry.path(), target)
                                                           : CopyFile(entry.path(), target);
        if (type_error || !copied) return false;
    }
    return true;
}

// Copies each save into ProsperoEden's folder for its kind (account_folder or device_folder, that is
// .../save/0000000000000000/<user ID or zeros>/<title ID>). What is already there is first moved
// to backup_root/<name>-account or -device, so nothing is lost; a failed copy puts it back.
inline bool Import(const std::vector<Save>& saves, const fs::path& account_folder, const fs::path& device_folder,
                   const fs::path& backup_root, const std::string& name, std::string& error) {
    for (const Save& save : saves) {
        const bool account = save.kind == Kind::Account;
        const fs::path& target = account ? account_folder : device_folder;
        const fs::path backup = backup_root / (name + (account ? "-account" : "-device"));
        std::error_code status_error, create_error;
        const bool had_save = fs::exists(target, status_error);
        if (status_error) {
            error = "cannot read " + target.string();
            return false;
        }
        if (had_save) {
            fs::create_directories(backup_root, create_error);
            if (create_error || std::rename(target.c_str(), backup.c_str()) != 0) {
                error = "cannot back up " + target.string();
                return false;
            }
        }
        if (!CopyTree(save.folder, target)) {
            std::error_code cleanup_error;
            fs::remove_all(target, cleanup_error);
            if (had_save) std::rename(backup.c_str(), target.c_str());
            error = "copy failed from " + save.folder.string();
            return false;
        }
    }
    return true;
}
} // namespace Eden::RyujinxSaves
