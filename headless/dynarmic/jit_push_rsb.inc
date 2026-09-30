    // ProsperoEden: EmitX64::PushRSBHelper with the return target linked through the JitGroup
    // (jit_group.h): `mov rcx, imm64` with the immediate 8-byte aligned, relinked atomically.
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    const IR::LocationDescriptor target{args[0].GetImmediateU64()};

    ctx.reg_alloc.ScratchGpr(code, HostLoc::RCX);
    const Xbyak::Reg64 loc_desc_reg = ctx.reg_alloc.ScratchGpr(code);
    const Xbyak::Reg64 index_reg = ctx.reg_alloc.ScratchGpr(code);

    code.mov(index_reg.cvt32(), dword[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_rsb_ptr]);
    code.mov(loc_desc_reg, target.Value());
    const std::size_t misalignment = (reinterpret_cast<std::uintptr_t>(code.getCurr()) + 2) & 7;
    if (misalignment)
        code.nop(8 - misalignment);
    {
        // Emitted unlinked; linked when the block is registered (JitGroup::Register).
        const auto* site = code.getCurr<const u8*>();
        const auto* unlinked = static_cast<const u8*>(code.GetReturnFromRunCodeAddress());
        code.db(0x48);  // mov rcx, imm64
        code.db(0xB9);
        code.dq(reinterpret_cast<u64>(unlinked));
        eden_pending_sites.push_back({target, {site, code.writableAddress(site), unlinked, JitGroup::SiteKind::Imm64At2}});
    }
    code.mov(qword[code.ABI_JIT_PTR + index_reg * 8 + code.GetJitStateInfo().offsetof_rsb_location_descriptors], loc_desc_reg);
    code.mov(qword[code.ABI_JIT_PTR + index_reg * 8 + code.GetJitStateInfo().offsetof_rsb_codeptrs], rcx);
    // Byte size hack
    DEBUG_ASSERT(code.GetJitStateInfo().rsb_ptr_mask <= 0xFF);
    code.add(index_reg.cvt32(), 1); //flags trashed, 1 single byte, haswell doesn't care
    code.and_(index_reg.cvt32(), u32(code.GetJitStateInfo().rsb_ptr_mask)); //trashes flags
    // Results ready and sort by least needed: give OOO some break
    code.mov(dword[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_rsb_ptr], index_reg.cvt32());
}
