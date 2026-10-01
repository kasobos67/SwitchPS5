// ProsperoEden - Whole-file reads for launcher assets.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

namespace pe
{

// Reads a whole file of at most max_bytes; false when it is missing, larger or unreadable.
inline bool read_file(const std::string &path, std::string *data, std::size_t max_bytes = 64u << 20)
{
    data->clear();
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char buffer[65536];
    bool ok = true;
    for (std::size_t count; (count = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
    {
        if (data->size() + count > max_bytes)
        {
            ok = false;
            break;
        }
        data->append(buffer, count);
    }
    ok = ok && std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

} // namespace pe
