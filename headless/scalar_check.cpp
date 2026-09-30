// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstdlib>
#include "dynarmic/frontend/A64/a64_ir_emitter.h"
#include "dynarmic/ir/opt_passes.h"

static void require(bool condition) { if (!condition) std::abort(); }

int main() {
    using namespace Dynarmic;
    unsigned cases = 0;
    for (unsigned width : {32u, 64u})
    for (unsigned source : {32u, 64u, 128u, 0u})
    for (unsigned lane : {0u, 1u})
    for (bool zero_upper : {false, true}) {
        IR::Block block{IR::LocationDescriptor{0}};
        A64::IREmitter ir{block};
        const IR::U32 input32 = ir.GetW(A64::Reg::R0);
        const IR::U64 scalar = source == 32 ? ir.ZeroExtendToLong(input32) :
                              source == 0 ? ir.Imm64(0xfedcba9876543210ULL) :
                                            ir.GetX(A64::Reg::R0);
        auto vector = source == 128 ? ir.GetQ(A64::Vec::V0) : ir.ZeroExtendToQuad(scalar);
        if (zero_upper) vector = ir.VectorZeroUpper(vector);
        const auto result = ir.VectorGetElement(width, vector, lane);
        if (width == 32) ir.SetW(A64::Reg::R1, IR::U32{result});
        else ir.SetX(A64::Reg::R1, IR::U64{result});
        ir.SetTerm(IR::Term::ReturnToDispatch{});
        A64::UserConfig config{};
        config.optimizations = OptimizationFlag::ConstProp; // Keep verification enabled.
        Optimization::Optimize(block, config, {});
        const bool folded = lane == 0 && source != 128 && (width == 64 || source != 64);
        unsigned extracts = 0, stores = 0;
        for (const auto& inst : block.Instructions()) {
            extracts += inst.GetOpcode() == IR::Opcode::VectorGetElement32 ||
                        inst.GetOpcode() == IR::Opcode::VectorGetElement64;
            if (inst.GetOpcode() != IR::Opcode::A64SetW &&
                inst.GetOpcode() != IR::Opcode::A64SetX) continue;
            ++stores;
            if (!folded) continue;
            const auto value = inst.GetArg(1);
            if (source == 0) {
                require(value.IsUnsignedImmediate(width == 32 ? 0x76543210ULL : 0xfedcba9876543210ULL));
            } else {
                require(!value.IsImmediate());
                require(value.GetInstRecursive() == (width == 32 ? IR::Value{input32} : IR::Value{scalar}).GetInstRecursive());
            }
        }
        require(stores == 1 && extracts == unsigned(!folded));
        ++cases;
    }
    std::printf("Scalar IR folding PASS: %u width/lane/extension cases\n", cases);
}
