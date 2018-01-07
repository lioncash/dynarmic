/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/ir/terminal.h"
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::InterpretThisInstruction() {
    ir.SetTerm(IR::Term::Interpret(ir.current_location));
    return false;
}

bool TranslatorVisitor::UnpredictableInstruction() {
    ASSERT_MSG(false, "UNPREDICTABLE");
    return false;
}

bool TranslatorVisitor::ReservedValue() {
    ASSERT_MSG(false, "RESERVEDVALUE");
    return false;
}

IR::U32U64 TranslatorVisitor::I(size_t bitsize, u64 value) {
    switch (bitsize) {
    case 32:
        return ir.Imm32(static_cast<u32>(value));
    case 64:
        return ir.Imm64(value);
    default:
        ASSERT_MSG(false, "Imm - get: Invalid bitsize");
        return {};
    }
}

IR::U32U64 TranslatorVisitor::X(size_t bitsize, Reg reg) {
    switch (bitsize) {
    case 32:
        return ir.GetW(reg);
    case 64:
        return ir.GetX(reg);
    default:
        ASSERT_MSG(false, "X - get: Invalid bitsize");
        return {};
    }
}

void TranslatorVisitor::X(size_t bitsize, Reg reg, IR::U32U64 value) {
    switch (bitsize) {
    case 32:
        ir.SetW(reg, value);
        return;
    case 64:
        ir.SetX(reg, value);
        return;
    default:
        ASSERT_MSG(false, "X - set: Invalid bitsize");
    }
}

IR::U32U64 TranslatorVisitor::SP(size_t bitsize) {
    switch (bitsize) {
    case 32:
        return ir.LeastSignificantWord(ir.GetSP());
    case 64:
        return ir.GetSP();
    default:
        ASSERT_MSG(false, "SP - get : Invalid bitsize");
        return {};
    }
}

void TranslatorVisitor::SP(size_t bitsize, IR::U32U64 value) {
    switch (bitsize) {
    case 32:
        ir.SetSP(ir.ZeroExtendWordToLong(value));
        break;
    case 64:
        ir.SetSP(value);
        break;
    default:
        ASSERT_MSG(false, "SP -  : Invalid bitsize");
    }
}

IR::U32U64 TranslatorVisitor::ShiftReg(size_t bitsize, Reg reg, Imm<2> shift, IR::U8 amount) {
    auto result = X(bitsize, reg);
    switch (shift.ZeroExtend()) {
    case 0b00:
        return ir.LogicalShiftLeft(result, amount);
    case 0b01:
        return ir.LogicalShiftRight(result, amount);
    case 0b10:
        return ir.ArithmeticShiftRight(result, amount);
    case 0b11:
        return ir.RotateRight(result, amount);
    }
    ASSERT_MSG(false, "Unreachable");
    return {};
}

IR::U32U64 TranslatorVisitor::ExtendReg(size_t bitsize, Reg reg, Imm<3> option, u8 shift) {
    ASSERT(shift <= 4);
    ASSERT(bitsize == 32 || bitsize == 64);
    IR::UAny val = X(bitsize, reg);
    size_t len;
    IR::U32U64 extended;
    bool signed_extend;

    switch (option.ZeroExtend()) {
    case 0b000: { // UXTB
        val = ir.LeastSignificantByte(val);
        len = 8;
        signed_extend = false;
        break;
    }
    case 0b001: { // UXTH
        val = ir.LeastSignificantHalf(val);
        len = 16;
        signed_extend = false;
        break;
    }
    case 0b010: { // UXTW
        if (bitsize != 32) {
            val = ir.LeastSignificantWord(val);
        }
        len = 32;
        signed_extend = false;
        break;
    }
    case 0b011: { // UXTX
        len = 64;
        signed_extend = false;
        break;
    }
    case 0b100: { // SXTB
        val = ir.LeastSignificantByte(val);
        len = 8;
        signed_extend = true;
        break;
    }
    case 0b101: { // SXTH
        val = ir.LeastSignificantHalf(val);
        len = 16;
        signed_extend = true;
        break;
    }
    case 0b110: { // SXTW
        if (bitsize != 32) {
            val = ir.LeastSignificantWord(val);
        }
        len = 32;
        signed_extend = true;
        break;
    }
    case 0b111: { // SXTX
        len = 64;
        signed_extend = true;
        break;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }

    if (len < bitsize) {
        if (bitsize == 32) {
            extended = signed_extend ? ir.SignExtendToWord(val) : ir.ZeroExtendToWord(val);
        } else {
            extended = signed_extend ? ir.SignExtendToLong(val) : ir.ZeroExtendToLong(val);
        }
    } else {
        extended = val;
    }

    return ir.LogicalShiftLeft(extended, ir.Imm8(shift));
}

} // namespace A64
} // namespace Dynarmic
