# Keep scalar SIMD accesses in XMM registers on the checked page-table path.
# Reuse the original scalar callback ABI; its temporary GPR is reserved before
# branching, and all register allocation remains outside deferred callbacks.
foreach(direction Read Write)
    string(TOLOWER "${direction}" lower)
    file(READ "${EDEN_PORT_DIR}/scalar-memory-${lower}.inc" scalar_access)
    string(FIND "${memory_source}" "void AxxEmitX64::EmitMemory${direction}(" begin)
    if(begin LESS 0)
        message(FATAL_ERROR "Pinned scalar memory emitter changed")
    endif()
    string(SUBSTRING "${memory_source}" 0 ${begin} prefix)
    string(SUBSTRING "${memory_source}" ${begin} -1 body)
    string(FIND "${body}" "    if (ordered && bitsize == 128)" offset)
    if(offset LESS 0)
        message(FATAL_ERROR "Pinned memory callback boundary changed")
    endif()
    string(SUBSTRING "${body}" 0 ${offset} before)
    string(SUBSTRING "${body}" ${offset} -1 after)
    set(memory_source "${prefix}${before}${scalar_access}${after}")
endforeach()

set(backend "${PROJECT_SOURCE_DIR}/src/dynarmic/src/dynarmic/backend/x64")
file(READ "${backend}/emit_x64_vector.cpp" scalar_registers)
set(low32 "    // TODO: DefineValue directly on Argument for index == 0\n\n    const Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr(code).cvt32();")
set(low64 [=[        // TODO: DefineValue directly on Argument for index == 0
        const Xbyak::Reg64 dest = ctx.reg_alloc.ScratchGpr(code).cvt64();
        auto const source = ctx.reg_alloc.UseXmm(code, args[0]);
        code.movq(dest, source);
        ctx.reg_alloc.DefineValue(code, inst, dest);]=])
foreach(anchor low32 low64)
    string(FIND "${scalar_registers}" "${${anchor}}" offset)
    if(offset LESS 0)
        message(FATAL_ERROR "Pinned scalar lane extraction changed")
    endif()
endforeach()
set(low_lane [=[        // Scratch ownership preserves any still-live full vector.
        const auto result = ctx.reg_alloc.UseScratchXmm(code, args[0]);
        ctx.reg_alloc.DefineValue(code, inst, result);]=])
string(REPLACE "${low32}" "    if (index == 0) {\n${low_lane}\n        return;\n    }\n\n    const Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr(code).cvt32();" scalar_registers "${scalar_registers}")
string(REPLACE "${low64}" "${low_lane}" scalar_registers "${scalar_registers}")
write_derived("${PORT_BUILD_DIR}/scalar_emit_x64_vector.cpp" "${scalar_registers}")

file(READ "${backend}/emit_x64_data_processing.cpp" scalar_registers)
set(extend "void EmitX64::EmitZeroExtendWordToLong(EmitContext& ctx, IR::Inst* inst) {\n    auto args = ctx.reg_alloc.GetArgumentInfo(inst);")
string(FIND "${scalar_registers}" "${extend}" offset)
if(offset LESS 0)
    message(FATAL_ERROR "Pinned scalar zero extension changed")
endif()
string(REPLACE "${extend}" "${extend}
    if (args[0].IsInXmm(ctx.reg_alloc) && code.HasHostFeature(HostFeature::SSE41)) {
        const auto result = ctx.reg_alloc.UseScratchXmm(code, args[0]);
        code.pmovzxdq(result, result); // U64 consumes only the zero-extended low word.
        ctx.reg_alloc.DefineValue(code, inst, result);
        return;
    }" scalar_registers "${scalar_registers}")
write_derived("${PORT_BUILD_DIR}/scalar_emit_x64_data_processing.cpp" "${scalar_registers}")
get_target_property(jit_sources dynarmic SOURCES)
foreach(unit vector data_processing)
    list(FILTER jit_sources EXCLUDE REGEX "(^|/)emit_x64_${unit}\\.cpp$")
    list(APPEND jit_sources "${PORT_BUILD_DIR}/scalar_emit_x64_${unit}.cpp")
endforeach()
set_property(TARGET dynarmic PROPERTY SOURCES "${jit_sources}")
