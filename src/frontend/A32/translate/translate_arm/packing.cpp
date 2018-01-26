/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

bool ArmTranslatorVisitor::arm_PKHBT(Cond cond, Reg n, Reg d, Imm5 imm5, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), ShiftType::LSL, imm5, ir.Imm1(false)).result;
        auto lower_half = ir.And(ir.GetRegister(n), ir.Imm32(0x0000FFFF));
        auto upper_half = ir.And(shifted, ir.Imm32(0xFFFF0000));
        ir.SetRegister(d, ir.Or(lower_half, upper_half));
    }

    return true;
}

bool ArmTranslatorVisitor::arm_PKHTB(Cond cond, Reg n, Reg d, Imm5 imm5, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), ShiftType::ASR, imm5, ir.Imm1(false)).result;
        auto lower_half = ir.And(shifted, ir.Imm32(0x0000FFFF));
        auto upper_half = ir.And(ir.GetRegister(n), ir.Imm32(0xFFFF0000));
        ir.SetRegister(d, ir.Or(lower_half, upper_half));
    }

    return true;
}

} // namespace Dynarmic::A32
