// Bounded, opt-in checks of the runtime linked into the app. Does not change FP state.
#pragma once
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#ifdef PS5_NATIVE
#include <pthread_np.h>
#endif
#include <thread>
#include <initializer_list>
#if defined(EDEN_PS5_VULKAN) && !defined(EDEN_PS5_RADV)
extern "C" FILE* ps5vk_private_open_memstream(char**, size_t*);
#endif

namespace Eden {
inline void AuditSdkThread(const char* name) {
    unsigned mxcsr;
    unsigned short control;
    asm volatile("stmxcsr %0" : "=m"(mxcsr));
    asm volatile("fnstcw %0" : "=m"(control));
    float input = std::bit_cast<float>(1u), normal = std::bit_cast<float>(0x00800000u);
    const float one = 1.0f, half = 0.5f;
    const bool masked = (mxcsr & 0x1f80) == 0x1f80;
    if (masked) {
        asm volatile("mulss %1, %0" : "+x"(input) : "x"(one));
        asm volatile("mulss %1, %0" : "+x"(normal) : "x"(half));
        asm volatile("ldmxcsr %0" : : "m"(mxcsr)); // Restore any diagnostic exception flags.
    }
    pthread_attr_t attr;
    void* stack = nullptr;
    size_t size = 0;
    int rc = pthread_attr_init(&attr);
    if (!rc) {
#ifdef PS5_NATIVE
        rc = pthread_attr_get_np(pthread_self(), &attr);
#else
        rc = pthread_getattr_np(pthread_self(), &attr);
#endif
        if (!rc) rc = pthread_attr_getstack(&attr, &stack, &size);
        pthread_attr_destroy(&attr);
    }
    std::printf("EDEN_SDK_THREAD name=%s mxcsr=%08x x87=%04x denormal_tested=%d denormal_input=%08x denormal_output=%08x stack_rc=%d stack=%p bytes=%zu\n",
        name, mxcsr, control, masked, std::bit_cast<unsigned>(input), std::bit_cast<unsigned>(normal), rc, stack, size);
}
inline void AuditMemoryStream(const char* name, FILE* (*open)(char**, size_t*)) {
    char* data = nullptr;
    size_t bytes = 0;
    errno = 0;
    FILE* stream = open(&data, &bytes);
    if (!stream) {
        std::printf("EDEN_SDK_MEMSTREAM name=%s supported=0 errno=%d\n", name, errno);
        return;
    }
    bool ok = std::fwrite("abc", 1, 3, stream) == 3 && std::fflush(stream) == 0;
    ok = ok && data && bytes == 3 && std::memcmp(data, "abc", 4) == 0;
    ok = ok && std::fseek(stream, 1, SEEK_SET) == 0 && std::fputc('Z', stream) != EOF;
    const int closed = std::fclose(stream);
    ok = ok && !closed && data && bytes >= 2 && data[0] == 'a' && data[1] == 'Z';
    std::printf("EDEN_SDK_MEMSTREAM name=%s supported=1 pass=%d bytes=%zu\n", name, ok, bytes);
    std::free(data);
}
inline void AuditSdk() {
    AuditSdkThread("main");
    std::thread worker([] { AuditSdkThread("new-worker"); });
    worker.join();
    struct Case { const char* text; double value; size_t consumed; };
    for (const auto& test : {Case{"0.1", 0.1, 3}, Case{"-0.125tail", -0.125, 6},
                             Case{"1e-3", 0.001, 4}, Case{"+2.5E2", 250.0, 6}}) {
        char *end_d, *end_f;
        const double d = std::strtod(test.text, &end_d);
        const float f = std::strtof(test.text, &end_f);
        const bool ok = std::abs(d - test.value) < 1e-12 &&
            std::abs(static_cast<double>(f) - test.value) < 1e-6 &&
            end_d == test.text + test.consumed && end_f == test.text + test.consumed;
        std::printf("EDEN_SDK_PARSE input=%s pass=%d double=%.9g float=%.9g\n", test.text, ok, d, f);
    }
    AuditMemoryStream("libc", open_memstream);
#if defined(EDEN_PS5_VULKAN) && !defined(EDEN_PS5_RADV)
    AuditMemoryStream("vulkan-compiler", ps5vk_private_open_memstream);
#endif
    constexpr size_t bytes = 512u * 1024u * 1024u;
    auto* memory = static_cast<volatile unsigned char*>(std::malloc(bytes));
    bool ok = memory != nullptr;
    if (memory) {
        for (size_t offset = 0; offset < bytes; offset += 65536) memory[offset] = 0x5a;
        for (size_t offset = 0; offset < bytes; offset += 65536) ok = ok && memory[offset] == 0x5a;
    }
    std::printf("EDEN_SDK_HEAP requested=%zu sparse_touch_stride=65536 pass=%d address=%p\n",
        bytes, ok, const_cast<unsigned char*>(memory));
    std::free(const_cast<unsigned char*>(memory));
}
}
