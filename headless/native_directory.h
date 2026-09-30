// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <unistd.h>
#include <vector>

extern "C" int sceKernelGetdents(int, char*, int);

namespace Eden {
inline std::vector<std::filesystem::directory_entry> ReadNativeDirectory(
        const std::filesystem::path& path, std::error_code& error) {
    std::vector<std::filesystem::directory_entry> entries;
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        error = {errno, std::generic_category()};
        std::fprintf(stderr, "EDEN_DIRECTORY open error=%d path=%s\n", error.value(), path.c_str());
        return entries;
    }
    struct Close { int fd; ~Close() { close(fd); } } close_fd{fd};
    // Mounted PS5 filesystems can require directory reads larger than 8 KiB.
    // FS service threads have small stacks; keep the kernel read buffer on heap.
    std::vector<char> buffer(65536);
    error.clear();
    for (;;) {
        const int count = sceKernelGetdents(fd, buffer.data(), buffer.size());
        if (count == 0) return entries;
        if (count < 0 || count > static_cast<int>(buffer.size())) {
            std::fprintf(stderr, "EDEN_DIRECTORY getdents result=%d errno=%d path=%s\n", count, errno, path.c_str());
            error = std::make_error_code(std::errc::io_error);
            return {};
        }
        std::size_t offset = 0;
        while (offset < static_cast<std::size_t>(count)) {
            constexpr auto name_offset = offsetof(dirent, d_name);
            const auto available = static_cast<std::size_t>(count) - offset;
            if (available <= name_offset) {
                error = std::make_error_code(std::errc::io_error); return {};
            }
            uint16_t length;
            std::memcpy(&length, buffer.data() + offset + offsetof(dirent, d_reclen), sizeof(length));
            if (length <= name_offset || length > available) {
                std::fprintf(stderr, "EDEN_DIRECTORY record length=%u available=%zu name_offset=%zu path=%s\n", length, available, name_offset, path.c_str());
                error = std::make_error_code(std::errc::io_error); return {};
            }
            const char* name = buffer.data() + offset + name_offset;
            const char* end = static_cast<const char*>(std::memchr(name, 0, length - name_offset));
            if (!end || end == name || std::memchr(name, '/', end - name)) {
                error = std::make_error_code(std::errc::io_error); return {};
            }
            const std::string_view filename{name, static_cast<std::size_t>(end - name)};
            if (filename != "." && filename != "..") {
                entries.emplace_back(path / filename, error);
                // Native lstat is denied even for readable app files. Eden's
                // directory callbacks use followed status, which stat supports.
                if (error == std::errc::operation_not_permitted) {
                    const auto status = entries.back().status(error);
                    if (!error && !std::filesystem::exists(status))
                        error = std::make_error_code(std::errc::no_such_file_or_directory);
                }
                if (error) {
                    std::fprintf(stderr, "EDEN_DIRECTORY entry error=%d path=%s name=%s\n", error.value(), path.c_str(), name);
                    return {};
                }
            }
            offset += length;
        }
    }
}
}
