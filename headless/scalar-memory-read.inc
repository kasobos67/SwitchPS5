    if constexpr (bitsize == 32 || bitsize == 64) {
        if (!fastmem_marker && args[2].GetImmediateAccType() == IR::AccType::VEC) {
            const auto vaddr = ctx.reg_alloc.UseGpr(code, args[1]);
            const auto temporary = ctx.reg_alloc.ScratchGpr(code);
            const auto vector = ctx.reg_alloc.ScratchXmm(code);
            const auto wrapped_fn = read_fallbacks[std::make_tuple(false, bitsize, vaddr.getIdx(), temporary.getIdx())];
            SharedLabel abort = ctx.GenSharedLabel(), end = ctx.GenSharedLabel();
            const auto pointer = EmitVAddrLookup(code, ctx, bitsize, *abort, vaddr);
            if constexpr (bitsize == 32) code.movd(vector, code.dword[pointer]);
            else code.movq(vector, code.qword[pointer]);
            ctx.deferred_emits.emplace_back([=, this, &ctx] {
                code.L(*abort);
                code.call(wrapped_fn);
                if constexpr (bitsize == 32) code.movd(vector, temporary.cvt32());
                else code.movq(vector, temporary);
                EmitCheckMemoryAbort(ctx, inst, end);
                code.jmp(*end, code.T_NEAR);
            });
            code.L(*end);
            ctx.reg_alloc.DefineValue(code, inst, vector);
            return;
        }
    }

