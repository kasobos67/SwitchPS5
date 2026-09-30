// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/logging.h"
#include <cstdio>
#include <cstdlib>

void Common::Log::FmtLogMessageImpl(Class, Level level, const char* file, unsigned line,
                                  const char*, fmt::string_view format,
                                  const fmt::format_args& args) {
    const auto message = fmt::vformat(format, args);
    std::fprintf(stderr, "Dynarmic[%u] %s:%u %s\n", unsigned(level), file, line, message.c_str());
    std::fflush(stderr);
}
void AssertFailSoftImpl() { std::abort(); }
[[noreturn]] void AssertFatalImpl() { std::abort(); }
