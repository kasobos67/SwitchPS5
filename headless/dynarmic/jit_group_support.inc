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

