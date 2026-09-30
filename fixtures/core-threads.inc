// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef EDEN_GUEST_THREADS_EXPECTED
SVC(current_core, 0x10)
static unsigned char worker_stacks[4][4096] __attribute__((aligned(16)));
static u64 counter, ready, go, done[4], sums[4], cores[4];

static void atomic_worker(u64 index) {
    cores[index] = ONE(current_core, 0).rc;
    __atomic_fetch_add(&ready, 1, __ATOMIC_ACQ_REL);
    while (!__atomic_load_n(&go, __ATOMIC_ACQUIRE)) ONE(sleep_thread, 100000);
    u64 sum = 0;
    for (u32 i = 0; i < 10000; ++i)
        sum += __atomic_fetch_add(&counter, 1, __ATOMIC_ACQ_REL);
    sums[index] = sum;
    cores[index] |= ONE(current_core, 0).rc << 32;
    __atomic_store_n(&done[index], 1, __ATOMIC_RELEASE);
    ONE(exit_thread, 0);
    for (;;) {}
}

static void check_threads(void) {
    stage = 61;
    u32 handles[4];
    for (u32 i = 0; i < 4; ++i) {
        Registers thread = create_thread(0, (u64)atomic_worker, i,
            (u64)(worker_stacks[i] + sizeof(worker_stacks[i])), 44, i);
        check(thread.rc);
        handles[i] = thread.first;
        check(ONE(start_thread, handles[i]).rc);
    }
    const u64 deadline = ONE(get_tick, 0).rc + 19200000ULL * 3;
    while (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != 4) {
        require(ONE(get_tick, 0).rc < deadline);
        ONE(sleep_thread, 100000);
    }
    __atomic_store_n(&go, 1, __ATOMIC_RELEASE);
    stage = 62;
    u64 sum = 0;
    for (u32 i = 0; i < 4; ++i) {
        while (!__atomic_load_n(&done[i], __ATOMIC_ACQUIRE)) {
            require(ONE(get_tick, 0).rc < deadline);
            ONE(sleep_thread, 100000);
        }
        // Read each worker's ordinary writes after acquire, before joining.
        sum += sums[i];
        require(cores[i] == ((u64)i << 32 | i));
        print_value("EDEN_GUEST_THREAD_CORES=", cores[i]);
        check(wait(handles[i], 1000000000).rc);
        close(handles[i]);
    }
    stage = 63;
    const u64 total = __atomic_load_n(&counter, __ATOMIC_ACQUIRE);
    print_value("EDEN_GUEST_THREAD_COUNT=", total);
    print_value("EDEN_GUEST_THREAD_SUM=", sum);
    require(total == EDEN_GUEST_THREADS_EXPECTED);
    require(sum == 40000ULL * 39999 / 2);
    PRINT("EDEN_GUEST_THREADS_PASS");
}
#endif
