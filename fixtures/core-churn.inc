// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef EDEN_GUEST_CHURN
static unsigned char churn_stacks[4][4096] __attribute__((aligned(16)));
static unsigned char child_stacks[4][4096] __attribute__((aligned(16)));
static u64 churn_progress[4];
static void churn_child(u64 index) {
    (void)index;
    ONE(exit_thread, 0);
    for (;;) {}
}
static void churn_worker(u64 index) {
    for (u64 iteration = 1;; ++iteration) {
        Registers child = create_thread(0, (u64)churn_child, index,
            (u64)(child_stacks[index] + sizeof(child_stacks[index])), 44, index);
        check(child.rc); check(ONE(start_thread, child.first).rc);
        check(wait(child.first, 1000000000).rc); close(child.first);
        struct { u64 address, size; u32 rest[6]; } memory;
        check(query_memory((u64)&memory, 0, (u64)child_stacks[index], 0, 0, 0).rc);
        __atomic_store_n(&churn_progress[index], iteration, __ATOMIC_RELEASE);
    }
}
__attribute__((noreturn)) static void check_churn(void) {
    stage = 90;
    for (u32 i = 0; i < 4; ++i) {
        Registers worker = create_thread(0, (u64)churn_worker, i,
            (u64)(churn_stacks[i] + sizeof(churn_stacks[i])), 44, i);
        check(worker.rc); check(ONE(start_thread, worker.first).rc);
        close(worker.first);
    }
    const u64 deadline = ONE(get_tick, 0).rc + 19200000ULL * 3;
    for (u32 i = 0; i < 4; ++i) {
        while (__atomic_load_n(&churn_progress[i], __ATOMIC_ACQUIRE) < 8) {
            require(ONE(get_tick, 0).rc < deadline);
            ONE(sleep_thread, 100000);
        }
        print_value("EDEN_GUEST_CHURN_PROGRESS=", __atomic_load_n(&churn_progress[i], __ATOMIC_ACQUIRE));
    }
    PRINT("EDEN_GUEST_CHURN_ACTIVE_EXIT");
    PRINT("EDEN_CORE_FIXTURE_PASS");
    // Deliberately exit with workers/children active to exercise kernel cleanup.
    ONE(exit_process, 0);
    for (;;) {}
}
#endif
