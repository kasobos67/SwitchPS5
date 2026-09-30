
// ProsperoEden: callback arguments that differ between JITs sharing generated code (each A64
// core's callbacks object and JIT) are loaded from the JIT state register at the registered
// offset instead of being embedded (jit_group.h). Other arguments stay immediates.
namespace {
struct EdenStateArg {
    std::atomic<u64> value{0};
    std::atomic<u32> offset{0};
};
std::array<EdenStateArg, 64> eden_state_args;

std::optional<u32> EdenStateArgOffset(u64 value) {
    if (value <= 1)
        return std::nullopt;
    for (const auto& arg : eden_state_args)
        if (arg.value.load(std::memory_order_acquire) == value)
            return arg.offset.load(std::memory_order_relaxed);
    return std::nullopt;
}

void EdenLoadArg(BlockOfCode& code, Xbyak::Reg64 reg, u64 arg) {
    if (const auto offset = EdenStateArgOffset(arg))
        code.mov(reg, code.qword[code.ABI_JIT_PTR + *offset]);
    else
        code.mov(reg, arg);
}
}  // namespace

extern "C" void eden_state_arg_add(u64 value, u32 offset) {
    for (auto& arg : eden_state_args) {
        u64 expected = 0;
        if (arg.value.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            arg.offset.store(offset, std::memory_order_relaxed);
            arg.value.store(value, std::memory_order_release);
            return;
        }
    }
    std::abort();  // more live A64 JITs than slots
}

extern "C" void eden_state_arg_remove(u64 value) {
    for (auto& arg : eden_state_args) {
        u64 expected = value;
        if (arg.value.compare_exchange_strong(expected, 0, std::memory_order_acq_rel))
            return;
    }
}

