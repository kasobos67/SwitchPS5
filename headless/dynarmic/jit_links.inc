// ProsperoEden: direct branches between blocks go through the process's JitGroup (jit_group.h).
// The branch's 32-bit displacement is 4-byte aligned, so linking it when `next` gets compiled
// (by any core) and unlinking it when `next` is invalidated are single atomic stores; while
// unlinked it jumps to a stub that stores next's PC and returns to the dispatcher. Sites are
// emitted unlinked and resolved when the block is registered.
enum class EdenLinkCheck { None, Halt, Cycles };

void EdenEmitLink(A64EmitX64& e, IR::LocationDescriptor next, EdenLinkCheck check) {
    BlockOfCode& code = e.code;
    const u64 next_pc = A64::LocationDescriptor{next}.PC();
    if (check == EdenLinkCheck::Halt)
        code.cmp(dword[code.ABI_JIT_PTR + offsetof(A64JitState, halt_reason)], 0);
    else if (check == EdenLinkCheck::Cycles)
        code.cmp(qword[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, cycles_remaining)], 0);
    const std::size_t opcode = check == EdenLinkCheck::None ? 1 : 2;
    const std::size_t misalignment = (reinterpret_cast<std::uintptr_t>(code.getCurr()) + opcode) & 3;
    if (misalignment)
        code.nop(4 - misalignment);
    const auto* site = code.getCurr<const u8*>();
    Xbyak::Label stub;
    if (check == EdenLinkCheck::Halt)
        code.jz(stub, code.T_NEAR);
    else if (check == EdenLinkCheck::Cycles)
        code.jg(stub, code.T_NEAR);
    else
        code.jmp(stub, code.T_NEAR);
    if (check != EdenLinkCheck::None) {
        // Halted, or out of cycles: leave the JIT with next's PC.
        code.mov(rax, next_pc);
        code.mov(qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        code.ForceReturnFromRunCode();
    }
    code.L(stub);
    code.mov(rax, next_pc);
    code.mov(qword[code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
    code.ReturnFromRunCode();
    // Linked when the block is registered (JitGroup::Register), under the group's lock.
    e.eden_pending_sites.push_back({next, {site, code.writableAddress(site), stub.getAddress(),
                                           check == EdenLinkCheck::None ? JitGroup::SiteKind::Rel32At1 : JitGroup::SiteKind::Rel32At2}});
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::LinkBlock terminal, IR::LocationDescriptor, bool is_single_step) {
    // Used for patches and linking
    if (e.conf.HasOptimization(OptimizationFlag::BlockLinking) && !is_single_step) {
        EdenEmitLink(e, terminal.next, e.conf.enable_cycle_counting ? EdenLinkCheck::Cycles : EdenLinkCheck::Halt);
    } else {
        e.code.mov(rax, A64::LocationDescriptor{terminal.next}.PC());
        e.code.mov(qword[e.code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        e.code.ReturnFromRunCode();
    }
    return true;
}

bool EmitTerminalImpl(A64EmitX64& e, IR::Term::LinkBlockFast terminal, IR::LocationDescriptor, bool is_single_step) {
    if (e.conf.HasOptimization(OptimizationFlag::BlockLinking) && !is_single_step) {
        EdenEmitLink(e, terminal.next, EdenLinkCheck::None);
    } else {
        e.code.mov(rax, A64::LocationDescriptor{terminal.next}.PC());
        e.code.mov(qword[e.code.ABI_JIT_PTR + offsetof(A64JitState, pc)], rax);
        e.code.ReturnFromRunCode();
    }
    return true;
}

