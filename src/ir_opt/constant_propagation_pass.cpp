/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <dynarmic/A32/callbacks.h>

#include "frontend/ir/basic_block.h"
#include "frontend/ir/opcodes.h"
#include "ir_opt/passes.h"

namespace Dynarmic {
namespace Optimization {

void ConstantPropagation(IR::Block& block, const A32::UserCallbacks::Memory& memory_callbacks) {
    for (auto& inst : block) {
        switch (inst.GetOpcode()) {
        case IR::Opcode::A32SetCFlag: {
            IR::Value arg = inst.GetArg(0);
            if (!arg.IsImmediate() && arg.GetInst()->GetOpcode() == IR::Opcode::A32GetCFlag) {
                inst.Invalidate();
            }
            break;
        }
        case IR::Opcode::LogicalShiftLeft:
        case IR::Opcode::LogicalShiftRight:
        case IR::Opcode::ArithmeticShiftRight:
        case IR::Opcode::RotateRight: {
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
        case IR::Opcode::A32ReadMemory8: {
            if (!inst.AreAllArgsImmediates())
                break;

            u32 vaddr = inst.GetArg(0).GetU32();
            if (memory_callbacks.IsReadOnlyMemory(vaddr)) {
                u8 value_from_memory = memory_callbacks.Read8(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
            }
            break;
        }
        case IR::Opcode::A32ReadMemory16: {
            if (!inst.AreAllArgsImmediates())
                break;

            u32 vaddr = inst.GetArg(0).GetU32();
            if (memory_callbacks.IsReadOnlyMemory(vaddr)) {
                u16 value_from_memory = memory_callbacks.Read16(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
            }
            break;
        }
        case IR::Opcode::A32ReadMemory32: {
            if (!inst.AreAllArgsImmediates())
                break;

            u32 vaddr = inst.GetArg(0).GetU32();
            if (memory_callbacks.IsReadOnlyMemory(vaddr)) {
                u32 value_from_memory = memory_callbacks.Read32(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
            }
            break;
        }
        case IR::Opcode::A32ReadMemory64: {
            if (!inst.AreAllArgsImmediates())
                break;

            u32 vaddr = inst.GetArg(0).GetU32();
            if (memory_callbacks.IsReadOnlyMemory(vaddr)) {
                u64 value_from_memory = memory_callbacks.Read64(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
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

} // namespace Optimization
} // namespace Dynarmic
