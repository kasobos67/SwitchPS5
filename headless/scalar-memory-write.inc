    if constexpr (bitsize == 32 || bitsize == 64) {
        if (!fastmem_marker && args[3].GetImmediateAccType() == IR::AccType::VEC) {
            const auto vaddr = ctx.reg_alloc.UseGpr(code, args[1]);
            const auto temporary = ctx.reg_alloc.ScratchGpr(code);
            const auto vector = ctx.reg_alloc.UseXmm(code, args[2]);
            const auto wrapped_fn = write_fallbacks[std::make_tuple(false, bitsize, vaddr.getIdx(), temporary.getIdx())];
            SharedLabel abort = ctx.GenSharedLabel(), end = ctx.GenSharedLabel();
            const auto pointer = EmitVAddrLookup(code, ctx, bitsize, *abort, vaddr);
            if constexpr (bitsize == 32) code.movd(code.dword[pointer], vector);
            else code.movq(code.qword[pointer], vector);
            ctx.deferred_emits.emplace_back([=, this, &ctx] {
                code.L(*abort);
                if constexpr (bitsize == 32) code.movd(temporary.cvt32(), vector);
                else code.movq(temporary, vector);
                code.call(wrapped_fn);
                EmitCheckMemoryAbort(ctx, inst, end);
                code.jmp(*end, code.T_NEAR);
            });
            code.L(*end);

            return;
        }
    }

