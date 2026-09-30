        case Op::VectorGetElement32:
        case Op::VectorGetElement64: {
            if (!inst.GetArg(1).IsZero()) break;
            auto* vector = inst.GetArg(0).GetInstRecursive();
            if (vector->GetOpcode() == Op::VectorZeroUpper)
                vector = vector->GetArg(0).GetInstRecursive();
            if (vector->GetOpcode() != Op::ZeroExtendLongToQuad) break;
            const auto scalar = vector->GetArg(0);
            if (inst.GetOpcode() == Op::VectorGetElement64) {
                inst.ReplaceUsesWith(scalar);
            } else if (scalar.IsImmediate()) {
                inst.ReplaceUsesWith(IR::Value{u32(scalar.GetU64())});
            } else if (scalar.GetInstRecursive()->GetOpcode() == Op::ZeroExtendWordToLong) {
                inst.ReplaceUsesWith(scalar.GetInstRecursive()->GetArg(0));
            }
            break;
        }
