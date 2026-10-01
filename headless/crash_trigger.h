// SPDX-License-Identifier: GPL-3.0-or-later
// Development builds: crash-app.txt in the app folder asks for a crash, to test the crash report
// (crash_report.h) on a console. Its one word says how: segv (a bad write on the thread that
// reads the file), thread (the same on a new thread), abort, or throw (an exception nothing
// catches).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace Eden::Crash {
inline void BadWrite() {
    static volatile std::uintptr_t address = 16;
    *reinterpret_cast<volatile int*>(address) = 1;
}

inline void DevelopmentRequest(const std::string& marker) {
    std::string how;
    {
        std::ifstream file(marker);
        if (!(file >> how)) return;
    }
    std::remove(marker.c_str());
    std::fprintf(stderr, "EDEN_DEV_CRASH how=%s\n", how.c_str());
    if (how == "abort") std::abort();
    if (how == "throw") std::thread([] { throw std::runtime_error("development crash request"); }).detach();
    else if (how == "thread") std::thread(BadWrite).detach();
    else BadWrite();
}
} // namespace Eden::Crash
