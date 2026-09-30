// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef EDEN_GUEST_FP
static void fp_result(u64 actual, u64 expected) {
    print_value("EDEN_GUEST_FP_RESULT=", actual);
    require(actual == expected);
}

// Explicit instructions prevent constant folding from replacing the tested work.
#define FP32(instruction, a, b, c, expected) do { \
    const u32 input[] = {a, b, c}; u32 output; \
    __asm__ volatile("ldr s0, [%1]; ldr s1, [%1, #4]; ldr s2, [%1, #8]; " \
        instruction "; fmov %w0, s3" : "=r"(output) : "r"(input) \
        : "v0", "v1", "v2", "v3", "memory"); \
    fp_result(output, expected); \
} while (0)
#define FP64(instruction, a, b, c, expected) do { \
    const u64 input[] = {a, b, c}; u64 output; \
    __asm__ volatile("ldr d0, [%1]; ldr d1, [%1, #8]; ldr d2, [%1, #16]; " \
        instruction "; fmov %0, d3" : "=r"(output) : "r"(input) \
        : "v0", "v1", "v2", "v3", "memory"); \
    fp_result(output, expected); \
} while (0)

static void check_fp(void) {
    stage = 70;
    u64 saved_fpcr, saved_fpsr;
    __asm__ volatile("mrs %0, fpcr; mrs %1, fpsr; msr fpcr, xzr; msr fpsr, xzr"
        : "=r"(saved_fpcr), "=r"(saved_fpsr) : : "memory");
    FP32("fadd s3, s0, s1", 0x3fc00000, 0x40100000, 0, 0x40700000); // 1.5 + 2.25
    FP32("fmul s3, s0, s1", 0xc0400000, 0x3f000000, 0, 0xbfc00000); // -3 * .5
    FP32("fdiv s3, s0, s1", 0x40e00000, 0x40000000, 0, 0x40600000); // 7 / 2
    FP32("fsqrt s3, s0", 0x40800000, 0, 0, 0x40000000);
    FP32("fmadd s3, s0, s1, s2", 0x3fc00000, 0x40000000, 0x3e800000, 0x40500000);
    FP32("fneg s3, s0", 0, 0, 0, 0x80000000); // negative zero
    FP32("fadd s3, s0, s1", 1, 1, 0, 2); // gradual underflow, FZ disabled
    FP32("fmadd s3, s0, s1, s2", 0x3f800001, 0x3f7ffffe, 0xbf800000, 0xa8800000); // -2^-46, single rounding
    FP32("fdiv s3, s0, s1", 0x3f800000, 0, 0, 0x7f800000); // positive infinity
    FP64("fadd d3, d0, d1", 0x3ff8000000000000ULL, 0x4002000000000000ULL, 0, 0x400e000000000000ULL);
    FP64("fmul d3, d0, d1", 0xc008000000000000ULL, 0x3fe0000000000000ULL, 0, 0xbff8000000000000ULL);
    FP64("fdiv d3, d0, d1", 0x401c000000000000ULL, 0x4000000000000000ULL, 0, 0x400c000000000000ULL);
    FP64("fsqrt d3, d0", 0x4010000000000000ULL, 0, 0, 0x4000000000000000ULL);
    FP64("fmadd d3, d0, d1, d2", 0x3ff8000000000000ULL, 0x4000000000000000ULL, 0x3fd0000000000000ULL, 0x400a000000000000ULL);
    FP64("fneg d3, d0", 0, 0, 0, 0x8000000000000000ULL);
    FP64("fadd d3, d0, d1", 1, 1, 0, 2);
    FP64("fmadd d3, d0, d1, d2", 0x3ff0000000000001ULL, 0x3feffffffffffffeULL, 0xbff0000000000000ULL, 0xb970000000000000ULL);
    FP64("fdiv d3, d0, d1", 0x3ff0000000000000ULL, 0, 0, 0x7ff0000000000000ULL);
    stage = 71;
    const u32 positive[] = {0x40000000, 0x40400000, 0x40000000, 0x40000000};
    const u32 negative[] = {0xc0000000, 0xc0000000, 0xc0400000, 0xc0000000};
    for (u64 mode = 0; mode < 4; ++mode) {
        const u64 control = mode << 22;
        __asm__ volatile("msr fpcr, %0" : : "r"(control) : "memory");
        // Force a guest sleep/resume while each nondefault control is active.
        ONE(sleep_thread, 1000000);
        FP32("frinti s3, s0", 0x40200000, 0, 0, positive[mode]);
        FP32("frinti s3, s0", 0xc0200000, 0, 0, negative[mode]);
    }
    __asm__ volatile("msr fpcr, xzr" : : : "memory");
    stage = 72;
    const u32 integers[] = {0xffffffff, 0x80000000, 7, 123, 1, 2, 9, 456};
    u32 lanes[4];
    __asm__ volatile("ldr q0, [%1]; ldr q1, [%1, #16]; add v2.4s, v0.4s, v1.4s; str q2, [%0]"
        : : "r"(lanes), "r"(integers) : "v0", "v1", "v2", "memory");
    const u32 added[] = {0, 0x80000002, 16, 579};
    for (u32 i = 0; i < 4; ++i) fp_result(lanes[i], added[i]);
    __asm__ volatile("ldr q0, [%1]; ldr q1, [%1, #16]; mul v2.4s, v0.4s, v1.4s; str q2, [%0]"
        : : "r"(lanes), "r"(integers) : "v0", "v1", "v2", "memory");
    const u32 multiplied[] = {0xffffffff, 0, 63, 56088};
    for (u32 i = 0; i < 4; ++i) fp_result(lanes[i], multiplied[i]);
    const u32 floats[] = {0x3fc00000, 0xc0000000, 0x3e800000, 0x41000000,
                          0x40000000, 0x3f000000, 0x40800000, 0xbe800000};
    __asm__ volatile("ldr q0, [%1]; ldr q1, [%1, #16]; fmul v2.4s, v0.4s, v1.4s; str q2, [%0]"
        : : "r"(lanes), "r"(floats) : "v0", "v1", "v2", "memory");
    const u32 products[] = {0x40400000, 0xbf800000, 0x3f800000, 0xc0000000};
    for (u32 i = 0; i < 4; ++i) fp_result(lanes[i], products[i]);
    __asm__ volatile("ldr q0, [%1]; rev64 v2.4s, v0.4s; str q2, [%0]"
        : : "r"(lanes), "r"(integers) : "v0", "v2", "memory");
    const u32 reversed[] = {0x80000000, 0xffffffff, 123, 7};
    for (u32 i = 0; i < 4; ++i) fp_result(lanes[i], reversed[i]);
    // Mix pair loads, scalar arithmetic and full-vector stores. Scalar writes
    // must clear every upper bit, including after a preceding full Q write.
    const u64 scalar32[] = {0x401000003fc00000ULL, 0xffffffffffffffffULL};
    const u64 scalar64[] = {0x3ff8000000000000ULL, 0x4002000000000000ULL};
    u64 mixed[4];
    __asm__ volatile("ldr q4, [%1]; ldp s2, s3, [%1]; fmov s4, s2; "
        "fadd s4, s4, s3; fmul s4, s4, s2; str q4, [%0]; "
        "ldr q0, [%1]; fmov s1, s0; fmov d2, d1; str q2, [%0, #16]"
        : : "r"(mixed), "r"(scalar32) : "v0", "v1", "v2", "v3", "v4", "memory");
    fp_result(mixed[0], 0x40b40000); fp_result(mixed[1], 0);
    fp_result(mixed[2], 0x3fc00000); fp_result(mixed[3], 0);
    __asm__ volatile("ldr q4, [%1]; ldp d2, d3, [%1]; fmov d4, d2; "
        "fadd d4, d4, d3; fmul d4, d4, d2; str q4, [%0]; "
        "ldr q0, [%1]; fmov d1, d0; fmov s2, s1; str q2, [%0, #16]"
        : : "r"(mixed), "r"(scalar64) : "v0", "v1", "v2", "v3", "v4", "memory");
    fp_result(mixed[0], 0x4016800000000000ULL); fp_result(mixed[1], 0);
    fp_result(mixed[2], 0); fp_result(mixed[3], 0);
    __asm__ volatile("msr fpcr, %0; msr fpsr, %1" : : "r"(saved_fpcr), "r"(saved_fpsr) : "memory");
    stage = 73;
    // Negative build alters only this final expectation, after all real results.
    require(lanes[3] == EDEN_GUEST_FP);
    PRINT("EDEN_GUEST_FP_PASS");
}
#undef FP32
#undef FP64
#endif
