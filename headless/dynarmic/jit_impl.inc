// ProsperoEden: Jit::Impl with one compiled-code namespace per guest process (inserted into
// a64_interface.cpp in place of upstream's Impl; see headless/dynarmic/jit_group.h).
//
// Each core compiles into its own region, in parallel, and publishes the block in its process's
// JitGroup; every core then runs it. Measured in a large open-world game before this change: 992k
// compilations for 518k distinct blocks (48% compiled again by another core), 60-70% of the
// compilations in a gameplay stutter were such duplicates, and the per-core caches (256/192/192
// MiB) ran close to full while together holding each block up to three times.
//
// Protocol (all atomics sequentially consistent):
// - eden_state: Outside (not in Run/Step), Running (in generated code, including its callbacks),
//   Compiling (translating/emitting into this core's region), Lookup (in the dispatcher or at
//   Run entry, in C++, not touching the region).
// - A full clear (a region is full, or ClearCache) sets group->clearing, halts every member and
//   waits until none is Running or Compiling; only then are all regions reset, the group
//   forgotten and group->epoch advanced. Cores park in Lookup while clearing is set.
// - A code pointer obtained in Lookup is used only if, after switching to Running, no clear is in
//   progress and the epoch is unchanged; otherwise the lookup is repeated.
// - A range invalidation unlinks and forgets the affected blocks at once (their code stays until
//   the next full clear, so a core still inside one finishes it), then every member drops its
//   return stack buffer and their fast dispatch entries on its own thread before running again.
// - A host thread that is no guest core may compile into a core's region (EdenPrecompile, for a
//   saved block list). eden_compile_mutex serializes it with the core's own compilations, which
//   use the same translator, emitter and region; eden_pre_state is its Compiling mark, which a
//   clear waits for like a core's. It never clears: it stops while the region has a reserve left.
//   The core comes first: it announces itself in eden_compile_waiters before it takes the mutex,
//   and the precompiler only tries the mutex, and only while no core is announced, so a core
//   waits for at most the one block being emitted.
struct Jit::Impl final {
public:
    Impl(Jit* jit, UserConfig conf)
        : conf(conf)
        , block_of_code(EdenRunCodeCallbacks(conf.callbacks, &GetCurrentBlockThunk, this, conf), JitStateInfo{jit_state}, conf.code_cache_size, GenRCP(conf))
        , emitter(block_of_code, conf, jit)
        , polyfill_options(GenPolyfillOptions(block_of_code))
    {
        ASSERT(conf.page_table_address_space_bits >= 12 && conf.page_table_address_space_bits <= 64);
        // Cores share only when they share an address space and link blocks without cycle counting.
        const bool shared = eden_jit_shared && conf.page_table && !conf.enable_cycle_counting;
        group = EdenJoinGroup(shared ? static_cast<const void*>(conf.page_table) : this, this);
        emitter.eden_group = group.get();
        {
            std::lock_guard lock{group->mutex};
            group->precompile = &EdenPrecompileThunk;
            group->space = &EdenSpaceThunk;
        }
        eden_space.store(block_of_code.SpaceRemaining(), std::memory_order_relaxed);
        EdenInitState();
    }

    ~Impl() {
        {
            // A precompilation in this region finishes before the region goes.
            std::lock_guard precompiling{group->precompile_mutex};
            EdenLeaveGroup(*group, this);
        }
        eden_state_arg_remove(reinterpret_cast<u64>(conf.callbacks));
        eden_state_arg_remove(reinterpret_cast<u64>(this));
    }

    HaltReason Run() {
        ASSERT(!is_executing);
        EdenCount(conf.processor_id, EdenRuns);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&jit_state.halt_reason)));
        is_executing = true;
        const CodePtr current_code_ptr = EdenEnter([this] {
            // RSB optimization
            const u32 new_rsb_ptr = (jit_state.rsb_ptr - 1) & A64JitState::RSB_PTR_MASK;
            if (jit_state.GetUniqueHash() == jit_state.rsb_location_descriptors[new_rsb_ptr]) {
                jit_state.rsb_ptr = new_rsb_ptr;
                return CodePtr(jit_state.rsb_codeptrs[new_rsb_ptr]);
            }
            return CodePtr((uintptr_t(GetCurrentBlock()) + 15) & ~uintptr_t(15));
        });
        const HaltReason hr = block_of_code.RunCode(&jit_state, current_code_ptr);
        eden_state.store(EdenOutside, std::memory_order_seq_cst);
        PerformRequestedCacheInvalidation(hr);
        is_executing = false;
        return hr;
    }

    HaltReason Step() {
        ASSERT(!is_executing);
        PerformRequestedCacheInvalidation(static_cast<HaltReason>(Atomic::Load(&jit_state.halt_reason)));
        is_executing = true;
        const CodePtr code_ptr = EdenEnter([this] { return GetCurrentSingleStep(); });
        const HaltReason hr = block_of_code.StepCode(&jit_state, code_ptr);
        eden_state.store(EdenOutside, std::memory_order_seq_cst);
        PerformRequestedCacheInvalidation(hr);
        is_executing = false;
        return hr;
    }

    void ClearCache() {
        std::unique_lock lock{invalidation_mutex};
        invalidate_entire_cache = true;
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void InvalidateCacheRange(u64 start_address, size_t length) {
        std::unique_lock lock{invalidation_mutex};
        const auto end_address = static_cast<u64>(start_address + length - 1);
        const auto range = boost::icl::discrete_interval<u64>::closed(start_address, end_address);
        invalid_cache_ranges.add(range);
        HaltExecution(HaltReason::CacheInvalidation);
    }

    void Reset() {
        ASSERT(!is_executing);
        jit_state = {};
        EdenInitState();
    }

    void HaltExecution(HaltReason hr) {
        Atomic::Or(&jit_state.halt_reason, static_cast<u32>(hr));
    }

    void ClearHalt(HaltReason hr) {
        Atomic::And(&jit_state.halt_reason, ~static_cast<u32>(hr));
    }

    u64 GetSP() const {
        return jit_state.sp;
    }

    void SetSP(u64 value) {
        jit_state.sp = value;
    }

    u64 GetPC() const {
        return jit_state.pc;
    }

    void SetPC(u64 value) {
        jit_state.pc = value;
    }

    u64 GetRegister(size_t index) const {
        if (index == 31)
            return GetSP();
        return jit_state.reg.at(index);
    }

    void SetRegister(size_t index, u64 value) {
        if (index == 31)
            return SetSP(value);
        jit_state.reg.at(index) = value;
    }

    std::array<u64, 31> GetRegisters() const {
        return jit_state.reg;
    }

    void SetRegisters(const std::array<u64, 31>& value) {
        jit_state.reg = value;
    }

    Vector GetVector(size_t index) const {
        return {jit_state.vec.at(index * 2), jit_state.vec.at(index * 2 + 1)};
    }

    void SetVector(size_t index, Vector value) {
        jit_state.vec.at(index * 2) = value[0];
        jit_state.vec.at(index * 2 + 1) = value[1];
    }

    std::array<Vector, 32> GetVectors() const {
        std::array<Vector, 32> ret;
        static_assert(sizeof(ret) == sizeof(jit_state.vec));
        std::memcpy(ret.data(), jit_state.vec.data(), sizeof(jit_state.vec));
        return ret;
    }

    void SetVectors(const std::array<Vector, 32>& value) {
        static_assert(sizeof(value) == sizeof(jit_state.vec));
        std::memcpy(jit_state.vec.data(), value.data(), sizeof(jit_state.vec));
    }

    u32 GetFpcr() const {
        return jit_state.GetFpcr();
    }

    void SetFpcr(u32 value) {
        jit_state.SetFpcr(value);
    }

    u32 GetFpsr() const {
        return jit_state.GetFpsr();
    }

    void SetFpsr(u32 value) {
        jit_state.SetFpsr(value);
    }

    u32 GetPstate() const {
        return jit_state.GetPstate();
    }

    void SetPstate(u32 value) {
        jit_state.SetPstate(value);
    }

    void ClearExclusiveState() {
        jit_state.exclusive_state = 0;
    }

    bool IsExecuting() const {
        return is_executing;
    }

    std::string Disassemble() const {
        const size_t size = reinterpret_cast<const char*>(block_of_code.getCurr()) - reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        auto const* p = reinterpret_cast<const char*>(block_of_code.GetCodeBegin());
        return Common::DisassembleX64(p, p + size);
    }

private:
    enum : u32 { EdenOutside = 0, EdenRunning = 1, EdenCompiling = 2, EdenLookup = 3 };

    // Called by the dispatcher of whichever region the core is executing; thisptr is the running
    // core's Impl (loaded from its JIT state, see EdenRunCodeCallbacks).
    static CodePtr GetCurrentBlockThunk(void* thisptr) {
        Jit::Impl* this_ = static_cast<Jit::Impl*>(thisptr);
        EdenCount(this_->conf.processor_id, EdenThunks);
        return this_->EdenEnter([this_] { return this_->GetCurrentBlock(); });
    }

    IR::LocationDescriptor GetCurrentLocation() const {
        return IR::LocationDescriptor{jit_state.GetUniqueHash()};
    }

    CodePtr GetCurrentBlock() {
        return GetBlock(GetCurrentLocation());
    }

    CodePtr GetCurrentSingleStep() {
        return GetBlock(A64::LocationDescriptor{GetCurrentLocation()}.SetSingleStepping(true));
    }

    // Looks up (or compiles) the code to enter while no clear can reset it, then marks the core
    // Running; repeats if a clear started or finished in between.
    template<typename Lookup>
    CodePtr EdenEnter(Lookup&& lookup) {
        for (;;) {
            eden_state.store(EdenLookup, std::memory_order_seq_cst);
            EdenWaitWhileClearing();
            EdenFlushIfRequested();
            const u64 epoch = group->epoch.load(std::memory_order_seq_cst);
            const CodePtr code_ptr = lookup();
            eden_state.store(EdenRunning, std::memory_order_seq_cst);
            if (!group->clearing.load(std::memory_order_seq_cst) &&
                group->epoch.load(std::memory_order_seq_cst) == epoch)
                return code_ptr;
        }
    }

    CodePtr GetBlock(IR::LocationDescriptor descriptor) {
        constexpr size_t MINIMUM_REMAINING_CODESIZE = 1 * 1024 * 1024;
        for (;;) {
            EdenWaitWhileClearing();
            const u64 epoch = group->epoch.load(std::memory_order_seq_cst);
            EdenCount(conf.processor_id, EdenLookups);
            {
                std::unique_lock lock{group->mutex, std::try_to_lock};
                if (!lock.owns_lock()) {
                    EdenCount(conf.processor_id, EdenContended);
                    lock.lock();
                }
                if (const auto* block = group->Find(descriptor)) {
                    EdenCount(conf.processor_id, EdenHits);
                    return block->entrypoint;
                }
            }
            if (block_of_code.SpaceRemaining() < MINIMUM_REMAINING_CODESIZE) {
                EdenReportPressure();
                EdenClearGroup();
                continue;
            }
            // Held until the block is published: a precompilation uses the same emitter.
            eden_compile_waiters.fetch_add(1, std::memory_order_seq_cst);
            std::lock_guard compiling{eden_compile_mutex};
            eden_compile_waiters.fetch_sub(1, std::memory_order_seq_cst);
            block_of_code.EnsureMemoryCommitted(MINIMUM_REMAINING_CODESIZE);

            const u32 previous = eden_state.exchange(EdenCompiling, std::memory_order_seq_cst);
            if (group->clearing.load(std::memory_order_seq_cst)) {
                eden_state.store(previous, std::memory_order_seq_cst);
                continue;
            }
            const u64 start = EdenClockNs();
            const auto get_code = [this](u64 vaddr) { return conf.callbacks->MemoryReadCode(vaddr); };
            // LocationDescriptor ctor() does important ops (like tflags) do not skip
            auto const arch_descriptor = A64::LocationDescriptor{descriptor};
            ir_block.Reset(arch_descriptor);
            A64::Translate(ir_block, arch_descriptor, get_code, {conf.define_unpredictable_behaviour, conf.wall_clock_cntpct});
            const u64 translated = EdenClockNs();
            Optimization::Optimize(ir_block, conf, polyfill_options);
            const u64 optimized = EdenClockNs();
            emitter.eden_pending_sites.clear();
            const auto emitted = emitter.Emit(ir_block, false);
            eden_state.store(previous, std::memory_order_seq_cst);
            EdenReportCompile(descriptor, start, translated, optimized);
            eden_space.store(block_of_code.SpaceRemaining(), std::memory_order_relaxed);

            std::lock_guard lock{group->mutex};
            if (group->epoch.load(std::memory_order_seq_cst) != epoch)
                continue;  // every region was reset after the emission: compile again
            JitGroup::Block registered{};
            group->Register(descriptor, {emitted.entrypoint, emitted.size}, emitter.eden_range_first,
                            emitter.eden_range_last, emitter.eden_pending_sites, registered);
            return registered.entrypoint;
        }
    }

    // Compiles `descriptor` into this core's region from a host thread that is no guest core.
    // 1: compiled and published. 0: already published, or a clear is under way. -2: the region is
    // at its reserve: half of it stays for the core's own compilations, so that a session which
    // goes somewhere the list does not cover is no closer to a clear than it would be with a
    // region half the size. -3: the core is compiling here.
    int EdenPrecompile(IR::LocationDescriptor descriptor) {
        const size_t RESERVE = conf.code_cache_size / 2;
        if (group->clearing.load(std::memory_order_seq_cst))
            return 0;
        u64 invalidations = 0;
        {
            std::lock_guard lock{group->mutex};
            if (group->Find(descriptor))
                return 0;
            invalidations = group->invalidations;
        }
        if (eden_compile_waiters.load(std::memory_order_seq_cst) != 0)
            return -3;
        std::unique_lock compiling{eden_compile_mutex, std::try_to_lock};
        if (!compiling.owns_lock() || eden_compile_waiters.load(std::memory_order_seq_cst) != 0)
            return -3;
        if (block_of_code.SpaceRemaining() < RESERVE)
            return -2;
        const u64 epoch = group->epoch.load(std::memory_order_seq_cst);
        eden_pre_state.store(EdenCompiling, std::memory_order_seq_cst);
        if (group->clearing.load(std::memory_order_seq_cst)) {
            eden_pre_state.store(EdenOutside, std::memory_order_seq_cst);
            return 0;
        }
        block_of_code.EnsureMemoryCommitted(1 * 1024 * 1024);
        const auto get_code = [this](u64 vaddr) { return conf.callbacks->MemoryReadCode(vaddr); };
        auto const arch_descriptor = A64::LocationDescriptor{descriptor};
        ir_block.Reset(arch_descriptor);
        A64::Translate(ir_block, arch_descriptor, get_code, {conf.define_unpredictable_behaviour, conf.wall_clock_cntpct});
        Optimization::Optimize(ir_block, conf, polyfill_options);
        emitter.eden_pending_sites.clear();
        const auto emitted = emitter.Emit(ir_block, false);
        eden_pre_state.store(EdenOutside, std::memory_order_seq_cst);
        eden_space.store(block_of_code.SpaceRemaining(), std::memory_order_relaxed);

        std::lock_guard lock{group->mutex};
        if (group->epoch.load(std::memory_order_seq_cst) != epoch)
            return 0;  // every region was reset after the emission
        if (group->invalidations != invalidations)
            return 0;  // the game changed code meanwhile: this block may be of the old code
        JitGroup::Block registered{};
        return group->Register(descriptor, {emitted.entrypoint, emitted.size}, emitter.eden_range_first,
                               emitter.eden_range_last, emitter.eden_pending_sites, registered) ? 1 : 0;
    }

    static int EdenPrecompileThunk(void* member, u64 location) {
        return static_cast<Impl*>(member)->EdenPrecompile(IR::LocationDescriptor{location});
    }

    // Room left in the member's region as of its last compilation (no lock: it is only a hint
    // for choosing the region with the most room).
    static std::size_t EdenSpaceThunk(void* member) {
        return static_cast<Impl*>(member)->eden_space.load(std::memory_order_relaxed);
    }

    void PerformRequestedCacheInvalidation(HaltReason hr) {
        if (Has(hr, HaltReason::CacheInvalidation)) {
            bool entire = false;
            boost::icl::interval_set<u64> ranges;
            {
                std::unique_lock lock{invalidation_mutex};
                ClearHalt(HaltReason::CacheInvalidation);
                entire = invalidate_entire_cache;
                invalidate_entire_cache = false;
                ranges.swap(invalid_cache_ranges);
            }
            if (entire) {
                EdenClearGroup();
            } else if (!ranges.empty()) {
                std::vector<u64> invalidated;
                std::vector<Impl*> members;
                {
                    std::lock_guard lock{group->mutex};
                    invalidated = group->Invalidate(ranges);
                    if (!invalidated.empty())
                        for (void* member : group->members)
                            members.push_back(static_cast<Impl*>(member));
                }
                for (Impl* member : members) {
                    member->EdenQueueFlush(invalidated);
                    if (member != this)
                        member->HaltExecution(HaltReason::CacheInvalidation);
                }
            }
        }
        EdenFlushIfRequested();
    }

    // Queues invalidated locations for this core's fast dispatch table (only its own thread may
    // write it: generated code reads entries without synchronization).
    void EdenQueueFlush(const std::vector<u64>& locations) {
        std::lock_guard lock{eden_flush_mutex};
        if (eden_flush_all || eden_flush_locations.size() + locations.size() > 4096) {
            eden_flush_all = true;
            eden_flush_locations.clear();
        } else {
            eden_flush_locations.insert(eden_flush_locations.end(), locations.begin(), locations.end());
        }
        eden_flush.store(true, std::memory_order_seq_cst);
    }

    // On this core's thread before it runs again: drops its return stack buffer and the fast
    // dispatch entries of invalidated blocks (the whole table after many invalidations).
    void EdenFlushIfRequested() {
        if (!eden_flush.exchange(false, std::memory_order_seq_cst))
            return;
        std::vector<u64> locations;
        bool all = false;
        {
            std::lock_guard lock{eden_flush_mutex};
            locations.swap(eden_flush_locations);
            all = std::exchange(eden_flush_all, false);
        }
        std::lock_guard lock{group->mutex};  // a full clear resets these tables too
        jit_state.ResetRSB();
        EdenCount(conf.processor_id, all ? EdenFlushAll : EdenFlushLocations, all ? 1 : locations.size());
        if (all) {
            emitter.ClearFastDispatchTable();
        } else if (emitter.fast_dispatch_table_lookup) {
            for (const u64 location : locations)
                (*emitter.fast_dispatch_table_lookup)(location) = {};
        }
    }

    void EdenWaitWhileClearing() {
        if (!group->clearing.load(std::memory_order_seq_cst))
            return;
        std::unique_lock lock{group->park_mutex};
        group->parked.wait(lock, [this] { return !group->clearing.load(std::memory_order_seq_cst); });
    }

    // Resets every region of the group; called outside generated code (Outside or Lookup).
    void EdenClearGroup() {
        bool expected = false;
        if (!group->clearing.compare_exchange_strong(expected, true, std::memory_order_seq_cst)) {
            EdenWaitWhileClearing();  // another member is clearing everything
            return;
        }
        std::vector<Impl*> members;
        {
            std::lock_guard lock{group->mutex};
            for (void* member : group->members)
                members.push_back(static_cast<Impl*>(member));
        }
        for (Impl* member : members)
            if (member != this)
                member->HaltExecution(HaltReason::CacheInvalidation);
        for (Impl* member : members) {
            if (member == this)
                continue;
            for (;;) {
                const u32 state = member->eden_state.load(std::memory_order_seq_cst);
                if (state != EdenRunning && state != EdenCompiling)
                    break;
                std::this_thread::yield();
            }
        }
        // A precompilation may be emitting into any member's region, this core's included.
        for (Impl* member : members)
            while (member->eden_pre_state.load(std::memory_order_seq_cst) == EdenCompiling)
                std::this_thread::yield();
        {
            std::lock_guard lock{group->mutex};
            // Eden asks every core's JIT for the same clear or invalidation: this clear satisfies
            // whatever the members had requested so far.
            for (Impl* member : members) {
                std::unique_lock requests{member->invalidation_mutex};
                member->invalidate_entire_cache = false;
                member->invalid_cache_ranges.clear();
            }
            for (Impl* member : members) {
                member->block_of_code.ClearCache();
                member->emitter.ClearCache();
                member->eden_space.store(member->block_of_code.SpaceRemaining(), std::memory_order_relaxed);
                member->jit_state.ResetRSB();
                std::lock_guard flush{member->eden_flush_mutex};
                member->eden_flush_locations.clear();
                member->eden_flush_all = false;
                member->eden_flush.store(false, std::memory_order_seq_cst);
            }
            group->Clear();
            group->epoch.fetch_add(1, std::memory_order_seq_cst);
        }
        {
            std::lock_guard lock{group->park_mutex};
            group->clearing.store(false, std::memory_order_seq_cst);
        }
        group->parked.notify_all();
    }

    // What generated code reads from the JIT state instead of embedding (a64_jitstate.h).
    void EdenInitState() {
        jit_state.eden_callbacks = conf.callbacks;
        jit_state.eden_jit = this;
        jit_state.eden_tpidr_el0 = conf.tpidr_el0;
        jit_state.eden_tpidrro_el0 = conf.tpidrro_el0;
        jit_state.eden_exclusive_address = conf.global_monitor ?
            GetExclusiveMonitorAddressPointer(conf.global_monitor, conf.processor_id) : nullptr;
        jit_state.eden_exclusive_value = conf.global_monitor ?
            GetExclusiveMonitorValuePointer(conf.global_monitor, conf.processor_id) : nullptr;
        jit_state.eden_conf = &conf;
        jit_state.eden_fast_dispatch = emitter.fast_dispatch_table.data();
    }

    void EdenReportPressure() {
        static thread_local unsigned pressure_count;
        if (++pressure_count <= 200)
            std::printf("EDEN_JIT_PRESSURE core=%u capacity=%u remaining=%zu count=%u mono_ns=%lld\n",
                        unsigned(conf.processor_id), unsigned(conf.code_cache_size), block_of_code.SpaceRemaining(),
                        pressure_count, static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch()).count()));
        if (eden_jit_evacuation)
            eden_jit_evacuation(unsigned(conf.processor_id));
    }

    // Compile timers. std::chrono::steady_clock is a system call on the PS5 (about 0.9 µs; four of
    // them per compiled block were 13-15% of compile time), so the port's clock reads the invariant
    // TSC instead; without it (host tools) the steady clock remains.
    static u64 EdenClockNs() {
        if (eden_jit_clock_ns)
            return eden_jit_clock_ns();
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void EdenReportCompile(IR::LocationDescriptor descriptor, u64 start, u64 translated, u64 optimized) {
        const u64 now = EdenClockNs();
        if (eden_jit_compile)
            eden_jit_compile(unsigned(conf.processor_id), now - start);
        if (eden_jit_phases)
            eden_jit_phases(unsigned(conf.processor_id), translated - start, optimized - translated,
                            now - optimized, descriptor.Value());
    }

    IR::Block ir_block = {LocationDescriptor(0, FP::FPCR(0), false)};
    const UserConfig conf;
    A64JitState jit_state;
    BlockOfCode block_of_code;
    A64EmitX64 emitter;
    Optimization::PolyfillOptions polyfill_options;
    bool is_executing = false;
    bool invalidate_entire_cache = false;
    boost::icl::interval_set<u64> invalid_cache_ranges;
    std::mutex invalidation_mutex;
    std::shared_ptr<JitGroup> group;
    std::atomic<u32> eden_state{EdenOutside};
    std::mutex eden_compile_mutex;
    std::atomic<u32> eden_compile_waiters{0};
    std::atomic<u32> eden_pre_state{EdenOutside};
    std::atomic<std::size_t> eden_space{0};
    std::atomic<bool> eden_flush{false};
    std::mutex eden_flush_mutex;
    std::vector<u64> eden_flush_locations;
    bool eden_flush_all = false;
};

