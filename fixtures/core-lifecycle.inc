// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef EDEN_GUEST_LIFECYCLE
static unsigned char state_stacks[2][4096] __attribute__((aligned(16)));
static u64 state_ready[2], state_errors[2];
static void state_worker(u64 index) {
    const u64 control = (index + 1) << 22;
    const u64 values[] = {0x1122334455667788ULL + index, 0x8877665544332211ULL + index,
                         0x123456789abcdef0ULL + index, 0xfedcba9876543210ULL + index};
    for (u64 round = 1; round <= 100; ++round) {
        __asm__ volatile("msr fpcr, %0; ldp q8, q31, [%1]" : : "r"(control), "r"(values)
            : "v8", "v31", "memory");
        __atomic_store_n(&state_ready[index], round, __ATOMIC_RELEASE);
        while (__atomic_load_n(&state_ready[1-index], __ATOMIC_ACQUIRE) < round)
            ONE(sleep_thread, 100000);
        u64 observed[6] = {0};
        __asm__ volatile("stp q8, q31, [%0]; mrs x9, fpcr; str x9, [%0, #32]; "
            "fmov s0, #2.5; frinti s0, s0; str s0, [%0, #40]"
            : : "r"(observed) : "x9", "v0", "memory");
        for (u32 lane = 0; lane < 4; ++lane)
            if (observed[lane] != values[lane]) state_errors[index] |= 1;
        if (observed[4] != control) state_errors[index] |= 2;
        if (observed[5] != (index ? 0x40000000 : 0x40400000)) state_errors[index] |= 4;
    }
    ONE(exit_thread, 0);
    for (;;) {}
}
static void check_state(void) {
    stage = 80;
    u32 handles[2];
    for (u32 i = 0; i < 2; ++i) {
        Registers thread = create_thread(0, (u64)state_worker, i,
            (u64)(state_stacks[i] + sizeof(state_stacks[i])), 44, 0);
        check(thread.rc); handles[i] = thread.first;
        check(ONE(start_thread, handles[i]).rc);
    }
    for (u32 i = 0; i < 2; ++i) {
        check(wait(handles[i], 3000000000ULL).rc); close(handles[i]);
        print_value("EDEN_GUEST_STATE_ERRORS=", state_errors[i]);
        require(state_errors[i] == 0 && state_ready[i] == 100);
    }
    PRINT("EDEN_GUEST_STATE_PASS");
}

SVC(map_memory, 0x4) SVC(unmap_memory, 0x5)
typedef struct { u64 address, size; u32 state, attribute, permission, ipc_refs, device_refs, pad; } MemoryInfo;
static MemoryInfo memory_info(u64 address) {
    MemoryInfo memory;
    check(query_memory((u64)&memory, 0, address, 0, 0, 0).rc);
    require(memory.size && memory.address <= address && memory.address + memory.size > address);
    return memory;
}
static void memory_error(u64 result, u64 expected) {
    print_value("EDEN_GUEST_MEMORY_ERROR=", result);
    require(result == expected);
}
static void check_memory(void) {
    stage = 81;
    const u64 unit = 2 * 1024 * 1024;
    Registers allocation = set_heap_size(0, unit, 0, 0, 0, 0); check(allocation.rc);
    const u64 base = allocation.first;
    volatile unsigned char* bytes = (volatile unsigned char*)base;
    for (u64 i = 0; i < unit; i += 4096) bytes[i] = (unsigned char)(i / 4096 + 7);
    Registers grown = set_heap_size(0, 2 * unit, 0, 0, 0, 0); check(grown.rc);
    require(grown.first == base && memory_info(base + 2 * unit - 1).permission == 3);
    for (u64 i = 0; i < unit; i += 4096) require(bytes[i] == (unsigned char)(i / 4096 + 7));
    bytes[unit] = 0xab;
    check(set_heap_size(0, unit, 0, 0, 0, 0).rc);
    require(memory_info(base + unit).state == 0);
    check(set_heap_size(0, 2 * unit, 0, 0, 0, 0).rc);
    require(bytes[unit] == 0);
    memory_error(set_heap_size(0, unit + 1, 0, 0, 0, 0).rc, 0xca01); // InvalidSize
    require(memory_info(base + 2 * unit - 1).permission == 3);
    stage = 82;
    u64 destination = info(14), end = destination + info(15); // Stack region
    for (u32 i = 0; i < 64 && destination < end; ++i) {
        MemoryInfo memory = memory_info(destination);
        if (memory.state == 0 && memory.address + memory.size - destination >= 4096) break;
        destination = memory.address + memory.size;
    }
    require(destination < end && memory_info(destination).state == 0);
    memory_error(map_memory(destination + 1, base, 4096, 0, 0, 0).rc, 0xcc01); // InvalidAddress
    memory_error(map_memory(destination, base, 0, 0, 0, 0).rc, 0xca01);
    for (u32 round = 0; round < 32; ++round) {
        check(map_memory(destination, base, 4096, 0, 0, 0).rc);
        require(memory_info(base).permission == 0 && memory_info(destination).permission == 3);
        volatile unsigned char* mapped = (volatile unsigned char*)destination;
        require(mapped[0] == (round ? round - 1 : 7));
        mapped[0] = (unsigned char)round;
        check(unmap_memory(destination, base, 4096, 0, 0, 0).rc);
        require(memory_info(destination).state == 0 && memory_info(base).permission == 3);
        require(bytes[0] == round);
    }
    memory_error(unmap_memory(destination, base, 4096, 0, 0, 0).rc, 0xd401); // InvalidCurrentMemory
    for (u64 i = 4096; i < unit; i += 4096) require(bytes[i] == (unsigned char)(i / 4096 + 7));
    check(set_heap_size(0, 0, 0, 0, 0, 0).rc);
    require(memory_info(base).state == 0);
    PRINT("EDEN_GUEST_MEMORY_PASS");
    stage = 83;
    require(EDEN_GUEST_LIFECYCLE == 1);
}
#endif
