// ProsperoEden - Launcher log lines (stderr; the app's log pipe stores them).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdarg>
#include <cstdio>

namespace pe::sys
{

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
inline void log(const char *format, ...)
{
    std::va_list arguments;
    va_start(arguments, format);
    std::fputs("[ProsperoEden] launcher: ", stderr);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}

} // namespace pe::sys
