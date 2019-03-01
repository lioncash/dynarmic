/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

// REV<c> <Rd>, <Rm>
bool ArmTranslatorVisitor::arm_REV(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto result = ir.ByteReverseWord(ir.GetRegister(m));
    ir.SetRegister(d, result);
    return true;
}

// REV16<c> <Rd>, <Rm>
bool ArmTranslatorVisitor::arm_REV16(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto reg_m = ir.GetRegister(m);
    const auto lo = ir.And(ir.LogicalShiftRight(reg_m, ir.Imm8(8), ir.Imm1(0)).result, ir.Imm32(0x00FF00FF));
    const auto hi = ir.And(ir.LogicalShiftLeft(reg_m, ir.Imm8(8), ir.Imm1(0)).result, ir.Imm32(0xFF00FF00));
    const auto result = ir.Or(lo, hi);

    ir.SetRegister(d, result);
    return true;
}

// REVSH<c> <Rd>, <Rm>
bool ArmTranslatorVisitor::arm_REVSH(Cond cond, Reg d, Reg m) {
    if (d == Reg::PC || m == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto rev_half = ir.ByteReverseHalf(ir.LeastSignificantHalf(ir.GetRegister(m)));
    ir.SetRegister(d, ir.SignExtendHalfToWord(rev_half));
    return true;
}

} // namespace Dynarmic::A32
