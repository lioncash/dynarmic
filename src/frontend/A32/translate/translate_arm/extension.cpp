/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic {
namespace A32 {

static IR::U32 Rotate(A32::IREmitter& ir, Reg m, SignExtendRotation rotate) {
    const u8 rotate_by = static_cast<u8>(static_cast<size_t>(rotate) * 8);
    return ir.RotateRight(ir.GetRegister(m), ir.Imm8(rotate_by), ir.Imm1(0)).result;
}

bool ArmTranslatorVisitor::arm_SXTAB(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // SXTAB <Rd>, <Rn>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto reg_n = ir.GetRegister(n);
        auto result = ir.Add(reg_n, ir.SignExtendByteToWord(ir.LeastSignificantByte(rotated)));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SXTAB16(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // SXTAB16 <Rd>, <Rn>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto low_byte = ir.And(rotated, ir.Imm32(0x00FF00FF));
        auto sign_bit = ir.And(rotated, ir.Imm32(0x00800080));
        auto addend = ir.Or(low_byte, ir.Mul(sign_bit, ir.Imm32(0x1FE)));
        auto result = ir.PackedAddU16(addend, ir.GetRegister(n)).result;
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SXTAH(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // SXTAH <Rd>, <Rn>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto reg_n = ir.GetRegister(n);
        auto result = ir.Add(reg_n, ir.SignExtendHalfToWord(ir.LeastSignificantHalf(rotated)));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SXTB(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // SXTB <Rd>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto result = ir.SignExtendByteToWord(ir.LeastSignificantByte(rotated));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SXTB16(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // SXTB16 <Rd>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto low_byte = ir.And(rotated, ir.Imm32(0x00FF00FF));
        auto sign_bit = ir.And(rotated, ir.Imm32(0x00800080));
        auto result = ir.Or(low_byte, ir.Mul(sign_bit, ir.Imm32(0x1FE)));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SXTH(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // SXTH <Rd>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto result = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(rotated));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UXTAB(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // UXTAB <Rd>, <Rn>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto reg_n = ir.GetRegister(n);
        auto result = ir.Add(reg_n, ir.ZeroExtendByteToWord(ir.LeastSignificantByte(rotated)));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UXTAB16(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();

    // UXTAB16 <Rd>, <Rn>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto result = ir.And(rotated, ir.Imm32(0x00FF00FF));
        auto reg_n = ir.GetRegister(n);
        result = ir.PackedAddU16(reg_n, result).result;
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UXTAH(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // UXTAH <Rd>, <Rn>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto reg_n = ir.GetRegister(n);
        auto result = ir.Add(reg_n, ir.ZeroExtendHalfToWord(ir.LeastSignificantHalf(rotated)));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UXTB(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // UXTB <Rd>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto result = ir.ZeroExtendByteToWord(ir.LeastSignificantByte(rotated));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UXTB16(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // UXTB16 <Rd>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto result = ir.And(rotated, ir.Imm32(0x00FF00FF));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UXTH(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
    if (d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // UXTH <Rd>, <Rm>, <rotate>
    if (ConditionPassed(cond)) {
        auto rotated = Rotate(ir, m, rotate);
        auto result = ir.ZeroExtendHalfToWord(ir.LeastSignificantHalf(rotated));
        ir.SetRegister(d, result);
    }
    return true;
}

} // namespace A32
} // namespace Dynarmic
