/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <dynarmic/A32/callbacks.h>

#include "frontend/ir/basic_block.h"
#include "frontend/ir/opcodes.h"
#include "ir_opt/passes.h"

namespace Dynarmic::Optimization {

void ConstantPropagation(IR::Block& block) {
    for (auto& inst : block) {
        switch (inst.GetOpcode()) {
        case IR::Opcode::LogicalShiftLeft32:
        case IR::Opcode::LogicalShiftRight32:
        case IR::Opcode::ArithmeticShiftRight32:
        case IR::Opcode::RotateRight32: {
            if (!inst.GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp)) {
                inst.SetArg(2, IR::Value(false));
            }

            auto shift_amount = inst.GetArg(1);
            if (shift_amount.IsImmediate() && shift_amount.GetU8() == 0) {
                IR::Inst* carry_inst = inst.GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);
                if (carry_inst) {
                    carry_inst->ReplaceUsesWith(inst.GetArg(2));
                }
                inst.ReplaceUsesWith(inst.GetArg(0));
            }
            break;
        }
        case IR::Opcode::ZeroExtendByteToWord: {
            if (!inst.AreAllArgsImmediates())
                break;

            u8 byte = inst.GetArg(0).GetU8();
            u32 value = static_cast<u32>(byte);
            inst.ReplaceUsesWith(IR::Value{value});
            break;
        }
        case IR::Opcode::ZeroExtendHalfToWord: {
            if (!inst.AreAllArgsImmediates())
                break;

            u16 half = inst.GetArg(0).GetU16();
            u32 value = static_cast<u32>(half);
            inst.ReplaceUsesWith(IR::Value{value});
            break;
        }
        default:
            break;
        }
    }
}

} // namespace Dynarmic::Optimization
