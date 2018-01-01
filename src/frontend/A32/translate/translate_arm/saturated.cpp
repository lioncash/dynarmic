/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic {
namespace A32 {

static IR::Value Pack2x16To1x32(A32::IREmitter& ir, IR::Value lo, IR::Value hi) {
    return ir.Or(ir.And(lo, ir.Imm32(0xFFFF)), ir.LogicalShiftLeft(hi, ir.Imm8(16), ir.Imm1(0)).result);
}

static IR::Value MostSignificantHalf(A32::IREmitter& ir, IR::Value value) {
    return ir.LeastSignificantHalf(ir.LogicalShiftRight(value, ir.Imm8(16), ir.Imm1(0)).result);
}

// Saturation instructions

bool ArmTranslatorVisitor::arm_SSAT(Cond cond, Imm5 sat_imm, Reg d, Imm5 imm5, bool sh, Reg n) {
    if (d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();

    size_t saturate_to = static_cast<size_t>(sat_imm) + 1;
    ShiftType shift = !sh ? ShiftType::LSL : ShiftType::ASR;

    // SSAT <Rd>, #<saturate_to>, <Rn>
    if (ConditionPassed(cond)) {
        auto operand = EmitImmShift(ir.GetRegister(n), shift, imm5, ir.GetCFlag());
        auto result = ir.SignedSaturation(operand.result, saturate_to);
        ir.SetRegister(d, result.result);
        ir.OrQFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SSAT16(Cond cond, Imm4 sat_imm, Reg d, Reg n) {
    if (d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();

    size_t saturate_to = static_cast<size_t>(sat_imm) + 1;

    // SSAT16 <Rd>, #<saturate_to>, <Rn>
    if (ConditionPassed(cond)) {
        auto lo_operand = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(ir.GetRegister(n)));
        auto hi_operand = ir.SignExtendHalfToWord(MostSignificantHalf(ir, ir.GetRegister(n)));
        auto lo_result = ir.SignedSaturation(lo_operand, saturate_to);
        auto hi_result = ir.SignedSaturation(hi_operand, saturate_to);
        ir.SetRegister(d, Pack2x16To1x32(ir, lo_result.result, hi_result.result));
        ir.OrQFlag(lo_result.overflow);
        ir.OrQFlag(hi_result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_USAT(Cond cond, Imm5 sat_imm, Reg d, Imm5 imm5, bool sh, Reg n) {
    if (d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();

    size_t saturate_to = static_cast<size_t>(sat_imm);
    ShiftType shift = !sh ? ShiftType::LSL : ShiftType::ASR;

    // USAT <Rd>, #<saturate_to>, <Rn>
    if (ConditionPassed(cond)) {
        auto operand = EmitImmShift(ir.GetRegister(n), shift, imm5, ir.GetCFlag());
        auto result = ir.UnsignedSaturation(operand.result, saturate_to);
        ir.SetRegister(d, result.result);
        ir.OrQFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_USAT16(Cond cond, Imm4 sat_imm, Reg d, Reg n) {
    if (d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();

    size_t saturate_to = static_cast<size_t>(sat_imm);

    // USAT16 <Rd>, #<saturate_to>, <Rn>
    if (ConditionPassed(cond)) {
        // UnsignedSaturation takes a *signed* value as input, hence sign extension is required.
        auto lo_operand = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(ir.GetRegister(n)));
        auto hi_operand = ir.SignExtendHalfToWord(MostSignificantHalf(ir, ir.GetRegister(n)));
        auto lo_result = ir.UnsignedSaturation(lo_operand, saturate_to);
        auto hi_result = ir.UnsignedSaturation(hi_operand, saturate_to);
        ir.SetRegister(d, Pack2x16To1x32(ir, lo_result.result, hi_result.result));
        ir.OrQFlag(lo_result.overflow);
        ir.OrQFlag(hi_result.overflow);
    }
    return true;
}

// Saturated Add/Subtract instructions

bool ArmTranslatorVisitor::arm_QADD(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // QADD <Rd>, <Rm>, <Rn>
    if (ConditionPassed(cond)) {
        auto a = ir.GetRegister(m);
        auto b = ir.GetRegister(n);
        auto result = ir.SignedSaturatedAdd(a, b);
        ir.SetRegister(d, result.result);
        ir.OrQFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_QSUB(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // QSUB <Rd>, <Rm>, <Rn>
    if (ConditionPassed(cond)) {
        auto a = ir.GetRegister(m);
        auto b = ir.GetRegister(n);
        auto result = ir.SignedSaturatedSub(a, b);
        ir.SetRegister(d, result.result);
        ir.OrQFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_QDADD(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // QDADD <Rd>, <Rm>, <Rn>
    if (ConditionPassed(cond)) {
        auto a = ir.GetRegister(m);
        auto b = ir.GetRegister(n);
        auto doubled = ir.SignedSaturatedAdd(b, b);
        ir.OrQFlag(doubled.overflow);
        auto result = ir.SignedSaturatedAdd(a, doubled.result);
        ir.SetRegister(d, result.result);
        ir.OrQFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_QDSUB(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // QDSUB <Rd>, <Rm>, <Rn>
    if (ConditionPassed(cond)) {
        auto a = ir.GetRegister(m);
        auto b = ir.GetRegister(n);
        auto doubled = ir.SignedSaturatedAdd(b, b);
        ir.OrQFlag(doubled.overflow);
        auto result = ir.SignedSaturatedSub(a, doubled.result);
        ir.SetRegister(d, result.result);
        ir.OrQFlag(result.overflow);
    }
    return true;
}

// Parallel saturated instructions

bool ArmTranslatorVisitor::arm_QASX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // QASX <Rd>, <Rn>, <Rm>
    if (ConditionPassed(cond)) {
        auto Rn = ir.GetRegister(n);
        auto Rm = ir.GetRegister(m);
        auto Rn_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(Rn));
        auto Rn_hi = ir.SignExtendHalfToWord(MostSignificantHalf(ir, Rn));
        auto Rm_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(Rm));
        auto Rm_hi = ir.SignExtendHalfToWord(MostSignificantHalf(ir, Rm));
        auto diff = ir.SignedSaturation(ir.Sub(Rn_lo, Rm_hi), 16).result;
        auto sum = ir.SignedSaturation(ir.Add(Rn_hi, Rm_lo), 16).result;
        auto result = Pack2x16To1x32(ir, diff, sum);
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_QSAX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // QSAX <Rd>, <Rn>, <Rm>
    if (ConditionPassed(cond)) {
        auto Rn = ir.GetRegister(n);
        auto Rm = ir.GetRegister(m);
        auto Rn_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(Rn));
        auto Rn_hi = ir.SignExtendHalfToWord(MostSignificantHalf(ir, Rn));
        auto Rm_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(Rm));
        auto Rm_hi = ir.SignExtendHalfToWord(MostSignificantHalf(ir, Rm));
        auto sum = ir.SignedSaturation(ir.Add(Rn_lo, Rm_hi), 16).result;
        auto diff = ir.SignedSaturation(ir.Sub(Rn_hi, Rm_lo), 16).result;
        auto result = Pack2x16To1x32(ir, sum, diff);
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UQASX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // UQASX <Rd>, <Rn>, <Rm>
    if (ConditionPassed(cond)) {
        auto Rn = ir.GetRegister(n);
        auto Rm = ir.GetRegister(m);
        auto Rn_lo = ir.ZeroExtendHalfToWord(ir.LeastSignificantHalf(Rn));
        auto Rn_hi = ir.ZeroExtendHalfToWord(MostSignificantHalf(ir, Rn));
        auto Rm_lo = ir.ZeroExtendHalfToWord(ir.LeastSignificantHalf(Rm));
        auto Rm_hi = ir.ZeroExtendHalfToWord(MostSignificantHalf(ir, Rm));
        auto diff = ir.UnsignedSaturation(ir.Sub(Rn_lo, Rm_hi), 16).result;
        auto sum = ir.UnsignedSaturation(ir.Add(Rn_hi, Rm_lo), 16).result;
        auto result = Pack2x16To1x32(ir, diff, sum);
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UQSAX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();

    // UQSAX <Rd>, <Rn>, <Rm>
    if (ConditionPassed(cond)) {
        auto Rn = ir.GetRegister(n);
        auto Rm = ir.GetRegister(m);
        auto Rn_lo = ir.ZeroExtendHalfToWord(ir.LeastSignificantHalf(Rn));
        auto Rn_hi = ir.ZeroExtendHalfToWord(MostSignificantHalf(ir, Rn));
        auto Rm_lo = ir.ZeroExtendHalfToWord(ir.LeastSignificantHalf(Rm));
        auto Rm_hi = ir.ZeroExtendHalfToWord(MostSignificantHalf(ir, Rm));
        auto sum = ir.UnsignedSaturation(ir.Add(Rn_lo, Rm_hi), 16).result;
        auto diff = ir.UnsignedSaturation(ir.Sub(Rn_hi, Rm_lo), 16).result;
        auto result = Pack2x16To1x32(ir, sum, diff);
        ir.SetRegister(d, result);
    }
    return true;
}

} // namespace A32
} // namespace Dynarmic
