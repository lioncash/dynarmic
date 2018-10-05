/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <dynarmic/A32/config.h>

#include "frontend/ir/basic_block.h"
#include "frontend/ir/opcodes.h"
#include "ir_opt/passes.h"

namespace Dynarmic::Optimization {
namespace {

// Folds AND operations based on the following:
//
// 1. imm_x & imm_y -> result
// 2. x & 0 -> 0
// 3. 0 & y -> 0
// 4. x & y -> y (where x has all bits set to 1)
// 5. x & y -> x (where y has all bits set to 1)
//
void FoldAND(IR::Inst& inst, bool is_32_bit) {
    const auto lhs = inst.GetArg(0);
    const auto rhs = inst.GetArg(1);

    const bool is_lhs_immediate = lhs.IsImmediate();
    const bool is_rhs_immediate = rhs.IsImmediate();

    if (is_lhs_immediate && is_rhs_immediate) {
        const u64 result = lhs.GetImmediateAsU64() & rhs.GetImmediateAsU64();

        if (is_32_bit) {
            inst.ReplaceUsesWith(IR::Value{static_cast<u32>(result)});
        } else {
            inst.ReplaceUsesWith(IR::Value{result});
        }
    } else if (lhs.IsZero() || rhs.IsZero()) {
        if (is_32_bit) {
            inst.ReplaceUsesWith(IR::Value{u32{0}});
        } else {
            inst.ReplaceUsesWith(IR::Value{u64{0}});
        }
    } else if (is_lhs_immediate && lhs.HasAllBitsSet()) {
        inst.ReplaceUsesWith(rhs);
    } else if (is_rhs_immediate && rhs.HasAllBitsSet()) {
        inst.ReplaceUsesWith(lhs);
    }
}

// Folds EOR operations based on the following:
//
// 1. imm_x ^ imm_y -> result
// 2. x ^ 0 -> x
// 3. 0 ^ y -> y
//
void FoldEOR(IR::Inst& inst, bool is_32_bit) {
    const auto lhs = inst.GetArg(0);
    const auto rhs = inst.GetArg(1);

    if (lhs.IsImmediate() && rhs.IsImmediate()) {
        const u64 result = lhs.GetImmediateAsU64() ^ rhs.GetImmediateAsU64();

        if (is_32_bit) {
            inst.ReplaceUsesWith(IR::Value{static_cast<u32>(result)});
        } else {
            inst.ReplaceUsesWith(IR::Value{result});
        }
    } else if (lhs.IsZero()) {
        inst.ReplaceUsesWith(rhs);
    } else if (rhs.IsZero()) {
        inst.ReplaceUsesWith(lhs);
    }
}

// Folds NOT operations if the contained value is an immediate.
void FoldNOT(IR::Inst& inst, bool is_32_bit) {
    const auto operand = inst.GetArg(0);

    if (operand.IsImmediate()) {
        const u64 result = ~operand.GetImmediateAsU64();

        if (is_32_bit) {
            inst.ReplaceUsesWith(IR::Value{static_cast<u32>(result)});
        } else {
            inst.ReplaceUsesWith(IR::Value{result});
        }
    }
}

// Folds OR operations based on the following:
//
// 1. imm_x | imm_y -> result
// 2. x | 0 -> x
// 3. 0 | y -> y
//
void FoldOR(IR::Inst& inst, bool is_32_bit) {
    const auto lhs = inst.GetArg(0);
    const auto rhs = inst.GetArg(1);

    if (lhs.IsImmediate() && rhs.IsImmediate()) {
        const u64 result = lhs.GetImmediateAsU64() | rhs.GetImmediateAsU64();

        if (is_32_bit) {
            inst.ReplaceUsesWith(IR::Value{static_cast<u32>(result)});
        } else {
            inst.ReplaceUsesWith(IR::Value{result});
        }
    } else if (lhs.IsZero()) {
        inst.ReplaceUsesWith(rhs);
    } else if (rhs.IsZero()) {
        inst.ReplaceUsesWith(lhs);
    }
}

void FoldSignExtendXToWord(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const s64 value = inst.GetArg(0).GetImmediateAsS64();
    inst.ReplaceUsesWith(IR::Value{static_cast<u32>(value)});
}

void FoldSignExtendXToLong(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const s64 value = inst.GetArg(0).GetImmediateAsS64();
    inst.ReplaceUsesWith(IR::Value{static_cast<u64>(value)});
}

void FoldZeroExtendXToWord(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const u64 value = inst.GetArg(0).GetImmediateAsU64();
    inst.ReplaceUsesWith(IR::Value{static_cast<u32>(value)});
}

void FoldZeroExtendXToLong(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const u64 value = inst.GetArg(0).GetImmediateAsU64();
    inst.ReplaceUsesWith(IR::Value{value});
}
} // Anonymous namespace

void ConstantPropagation(IR::Block& block) {
    for (auto& inst : block) {
        const auto opcode = inst.GetOpcode();

        switch (opcode) {
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
        case IR::Opcode::And32:
        case IR::Opcode::And64:
            FoldAND(inst, opcode == IR::Opcode::And32);
            break;
        case IR::Opcode::Eor32:
        case IR::Opcode::Eor64:
            FoldEOR(inst, opcode == IR::Opcode::Eor32);
            break;
        case IR::Opcode::Or32:
        case IR::Opcode::Or64:
            FoldOR(inst, opcode == IR::Opcode::Or32);
            break;
        case IR::Opcode::Not32:
        case IR::Opcode::Not64:
            FoldNOT(inst, opcode == IR::Opcode::Not32);
            break;
        case IR::Opcode::SignExtendByteToWord:
        case IR::Opcode::SignExtendHalfToWord:
            FoldSignExtendXToWord(inst);
            break;
        case IR::Opcode::SignExtendByteToLong:
        case IR::Opcode::SignExtendHalfToLong:
        case IR::Opcode::SignExtendWordToLong:
            FoldSignExtendXToLong(inst);
            break;
        case IR::Opcode::ZeroExtendByteToWord:
        case IR::Opcode::ZeroExtendHalfToWord:
            FoldZeroExtendXToWord(inst);
            break;
        case IR::Opcode::ZeroExtendByteToLong:
        case IR::Opcode::ZeroExtendHalfToLong:
        case IR::Opcode::ZeroExtendWordToLong:
            FoldZeroExtendXToLong(inst);
            break;
        default:
            break;
        }
    }
}

} // namespace Dynarmic::Optimization
