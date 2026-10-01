// SPDX-License-Identifier: GPL-3.0-or-later
// C library entry points the app supplies itself; the whole program links against these
// definitions.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);

extern "C" int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
    constexpr int running = 2;
    int state = __atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE);
    if (state == PTHREAD_DONE_INIT) return 0;

    int expected = PTHREAD_NEEDS_INIT;
    if (__atomic_compare_exchange_n(&once_control->state, &expected, running, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        init_routine();
        __atomic_store_n(&once_control->state, PTHREAD_DONE_INIT, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE) != PTHREAD_DONE_INIT) {
        sceKernelUsleep(100);
    }
    return 0;
}

extern "C" float strtof(const char* value, char** end) {
    return static_cast<float>(strtod(value, end));
}

extern "C" int fseek(std::FILE* file, long offset, int origin) {
    return fseeko(file, offset, origin);
}

extern "C" long ftell(std::FILE* file) {
    return static_cast<long>(ftello(file));
}

extern "C" char* strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; ++haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n) {
            const char hc = *h >= 'A' && *h <= 'Z' ? static_cast<char>(*h + ('a' - 'A')) : *h;
            const char nc = *n >= 'A' && *n <= 'Z' ? static_cast<char>(*n + ('a' - 'A')) : *n;
            if (hc != nc) break;
            ++h;
            ++n;
        }
        if (!*n) return const_cast<char*>(haystack);
    }
    return nullptr;
}
