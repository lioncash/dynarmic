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

} // namespace A64
} // namespace Dynarmic
