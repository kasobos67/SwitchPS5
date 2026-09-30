// PS5 x64: retain the storage ABI used by generated monitor operations and
// the original exchange + full-fence unlock ordering. No executable helper
// allocation or call_once is needed for compiler-provided lock-free atomics.
static_assert(__atomic_always_lock_free(sizeof(int), nullptr));
void SpinLock::Lock() noexcept {
    while (__atomic_exchange_n(&storage, 1, __ATOMIC_SEQ_CST)) {
        // Poll without repeatedly invalidating the owner's cache line.
        while (__atomic_load_n(&storage, __ATOMIC_RELAXED))
            __builtin_ia32_pause();
    }
}

void SpinLock::Unlock() noexcept {
    __atomic_exchange_n(&storage, 0, __ATOMIC_SEQ_CST);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
