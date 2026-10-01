// ProsperoEden: JitGroup membership (jit_group.h) and the prelude callbacks of a JIT whose code
// every core of its process may run.
extern "C" void eden_state_arg_add(u64 value, u32 offset);  // callback.cpp
extern "C" void eden_state_arg_remove(u64 value);
extern "C" {
// Development switch (dev-settings jit_shared=off): every core keeps its own blocks.
bool eden_jit_shared = true;
}

namespace {
// Dispatch-path counters per guest core, read by the development performance report
// (eden_jit_path_counters): Run entries, dispatcher lookups from generated code, block lookups
// and hits, contended group locks, whole and targeted fast-dispatch flushes.
enum EdenPath : unsigned { EdenRuns, EdenThunks, EdenLookups, EdenHits, EdenContended, EdenFlushAll, EdenFlushLocations, EdenPathCount };
struct alignas(64) EdenPathCounters {
    std::array<std::atomic<unsigned long long>, EdenPathCount> values{};
};
std::array<EdenPathCounters, 4> eden_path{};
void EdenCount(u32 core, EdenPath what, unsigned long long amount = 1) {
    eden_path[core < eden_path.size() ? core : eden_path.size() - 1].values[what].fetch_add(amount, std::memory_order_relaxed);
}
std::mutex eden_groups_mutex;
std::vector<std::weak_ptr<JitGroup>> eden_groups;

std::shared_ptr<JitGroup> EdenJoinGroup(const void* key, void* member) {
    std::lock_guard lock{eden_groups_mutex};
    std::erase_if(eden_groups, [](const std::weak_ptr<JitGroup>& weak) { return weak.expired(); });
    std::shared_ptr<JitGroup> group;
    for (const auto& weak : eden_groups) {
        if (auto candidate = weak.lock(); candidate && candidate->key == key) {
            group = std::move(candidate);
            break;
        }
    }
    if (!group) {
        group = std::make_shared<JitGroup>();
        group->key = key;
        eden_groups.push_back(group);
    }
    std::lock_guard members{group->mutex};
    group->members.push_back(member);
    return group;
}

void EdenLeaveGroup(JitGroup& group, void* member) {
    std::lock_guard lock{group.mutex};
    std::erase(group.members, member);
}

}  // namespace

namespace {
// The application's group: the shared one with the most member cores (system processes and
// unshared JITs have groups of their own).
std::shared_ptr<JitGroup> EdenApplicationGroup() {
    std::lock_guard lock{eden_groups_mutex};
    std::shared_ptr<JitGroup> best;
    std::size_t most = 1;
    for (const auto& weak : eden_groups) {
        auto group = weak.lock();
        if (!group)
            continue;
        std::lock_guard members{group->mutex};
        if (group->members.size() > most) {
            most = group->members.size();
            best = std::move(group);
        }
    }
    return best;
}
}  // namespace

// Precompilation for a host thread that is no guest core (headless/jit_list.h).
//
// eden_jit_list_open: a handle on the application's compiled blocks, or null when there is no
// shared application group (yet). Close it with eden_jit_list_close.
extern "C" void* eden_jit_list_open() {
    std::shared_ptr<JitGroup> group = EdenApplicationGroup();
    return group ? new std::shared_ptr<JitGroup>(std::move(group)) : nullptr;
}

extern "C" void eden_jit_list_close(void* handle) {
    delete static_cast<std::shared_ptr<JitGroup>*>(handle);
}

// Compiles the block at `location` (a location descriptor's value) into the member region with
// the most room that its core is not compiling in, so that the cores keep equal headroom for
// their own compilations and never wait. 1: compiled; 0: nothing to do (already compiled, or a
// clear is under way); 2: every usable region is busy, try this location again; -1: stop (the
// game's JITs are gone, or every region is at its reserve).
extern "C" int eden_jit_precompile(void* handle, unsigned long long location) {
    JitGroup* const group = handle ? static_cast<std::shared_ptr<JitGroup>*>(handle)->get() : nullptr;
    if (!group)
        return -1;
    std::lock_guard precompiling{group->precompile_mutex};
    std::array<void*, 8> members{};
    std::size_t count = 0;
    int (*compile)(void*, u64) = nullptr;
    std::size_t (*space)(void*) = nullptr;
    {
        std::lock_guard lock{group->mutex};
        for (void* member : group->members)
            if (count < members.size())
                members[count++] = member;
        compile = group->precompile;
        space = group->space;
    }
    if (!compile || !space || count == 0)
        return -1;
    // Most room first. The room is read once: it changes while the cores compile.
    std::array<std::pair<std::size_t, void*>, 8> order{};
    for (std::size_t i = 0; i < count; ++i)
        order[i] = {space(members[i]), members[i]};
    std::sort(order.begin(), order.begin() + count,
              [](const auto& a, const auto& b) { return a.first > b.first; });
    bool busy = false;
    for (std::size_t i = 0; i < count; ++i) {
        const int result = compile(order[i].second, location);
        if (result >= 0)
            return result;
        busy |= result == -3;
    }
    return busy ? 2 : -1;
}

// The application's published locations so far, oldest first: copies up to `capacity` of them and
// returns how many there are.
extern "C" std::size_t eden_jit_history(void* handle, unsigned long long* out, std::size_t capacity) {
    JitGroup* const group = handle ? static_cast<std::shared_ptr<JitGroup>*>(handle)->get() : nullptr;
    if (!group)
        return 0;
    std::lock_guard lock{group->mutex};
    const std::size_t count = std::min(capacity, group->history.size());
    for (std::size_t i = 0; i < count; ++i)
        out[i] = group->history[i];
    return group->history.size();
}

extern "C" void eden_jit_path_counters(unsigned core, unsigned long long* out) {
    if (core >= eden_path.size()) return;
    for (unsigned i = 0; i < EdenPathCount; ++i)
        out[i] = eden_path[core].values[i].load(std::memory_order_relaxed);
    out[EdenPathCount] = JitGroup::far_links.load(std::memory_order_relaxed);
}

namespace {
// Registers the JIT object and callbacks before the prelude is generated, so that its dispatcher
// and callback calls load the running core's own from the JIT state (callback.cpp).
RunCodeCallbacks EdenRunCodeCallbacks(A64::UserCallbacks* cb, CodePtr (*lookup_block)(void*), void* arg,
                                      const A64::UserConfig& conf) {
    eden_state_arg_add(reinterpret_cast<u64>(cb), u32(offsetof(A64JitState, eden_callbacks)));
    eden_state_arg_add(reinterpret_cast<u64>(arg), u32(offsetof(A64JitState, eden_jit)));
    return GenRunCodeCallbacks(cb, lookup_block, arg, conf);
}
}  // namespace

