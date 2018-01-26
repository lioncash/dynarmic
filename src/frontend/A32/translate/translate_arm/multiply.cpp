/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

// Multiply (Normal) instructions
bool ArmTranslatorVisitor::arm_MLA(Cond cond, bool S, Reg d, Reg a, Reg m, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || a == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.Add(ir.Mul(ir.GetRegister(n), ir.GetRegister(m)), ir.GetRegister(a));
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MUL(Cond cond, bool S, Reg d, Reg m, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.Mul(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
        }
    }
    return true;
}


// Multiply (Long) instructions
bool ArmTranslatorVisitor::arm_SMLAL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n64 = ir.SignExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.SignExtendWordToLong(ir.GetRegister(m));
        auto product = ir.Mul(n64, m64);
        auto addend = ir.Pack2x32To1x64(ir.GetRegister(dLo), ir.GetRegister(dHi));
        auto result = ir.Add(product, addend);
        auto lo = ir.LeastSignificantWord(result);
        auto hi = ir.MostSignificantWord(result).result;
        ir.SetRegister(dLo, lo);
        ir.SetRegister(dHi, hi);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(hi));
            ir.SetZFlag(ir.IsZero(result));
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMULL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n64 = ir.SignExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.SignExtendWordToLong(ir.GetRegister(m));
        auto result = ir.Mul(n64, m64);
        auto lo = ir.LeastSignificantWord(result);
        auto hi = ir.MostSignificantWord(result).result;
        ir.SetRegister(dLo, lo);
        ir.SetRegister(dHi, hi);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(hi));
            ir.SetZFlag(ir.IsZero(result));
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UMAAL(Cond cond, Reg dHi, Reg dLo, Reg m, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto lo64 = ir.ZeroExtendWordToLong(ir.GetRegister(dLo));
        auto hi64 = ir.ZeroExtendWordToLong(ir.GetRegister(dHi));
        auto n64 = ir.ZeroExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.ZeroExtendWordToLong(ir.GetRegister(m));
        auto result = ir.Add(ir.Add(ir.Mul(n64, m64), hi64), lo64);
        ir.SetRegister(dLo, ir.LeastSignificantWord(result));
        ir.SetRegister(dHi, ir.MostSignificantWord(result).result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UMLAL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto addend = ir.Pack2x32To1x64(ir.GetRegister(dLo), ir.GetRegister(dHi));
        auto n64 = ir.ZeroExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.ZeroExtendWordToLong(ir.GetRegister(m));
        auto result = ir.Add(ir.Mul(n64, m64), addend);
        auto lo = ir.LeastSignificantWord(result);
        auto hi = ir.MostSignificantWord(result).result;
        ir.SetRegister(dLo, lo);
        ir.SetRegister(dHi, hi);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(hi));
            ir.SetZFlag(ir.IsZero(result));
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UMULL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n64 = ir.ZeroExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.ZeroExtendWordToLong(ir.GetRegister(m));
        auto result = ir.Mul(n64, m64);
        auto lo = ir.LeastSignificantWord(result);
        auto hi = ir.MostSignificantWord(result).result;
        ir.SetRegister(dLo, lo);
        ir.SetRegister(dHi, hi);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(hi));
            ir.SetZFlag(ir.IsZero(result));
        }
    }
    return true;
}


// Multiply (Halfword) instructions
bool ArmTranslatorVisitor::arm_SMLALxy(Cond cond, Reg dHi, Reg dLo, Reg m, bool M, bool N, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n16 = N ? ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result
                     : ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m16 = M ? ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result
                     : ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto product = ir.SignExtendWordToLong(ir.Mul(n16, m16));
        auto addend = ir.Pack2x32To1x64(ir.GetRegister(dLo), ir.GetRegister(dHi));
        auto result = ir.Add(product, addend);
        ir.SetRegister(dLo, ir.LeastSignificantWord(result));
        ir.SetRegister(dHi, ir.MostSignificantWord(result).result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMLAxy(Cond cond, Reg d, Reg a, Reg m, bool M, bool N, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || a == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n16 = N ? ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result
                     : ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m16 = M ? ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result
                     : ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto product = ir.Mul(n16, m16);
        auto result_overflow = ir.AddWithCarry(product, ir.GetRegister(a), ir.Imm1(0));
        ir.SetRegister(d, result_overflow.result);
        ir.OrQFlag(result_overflow.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMULxy(Cond cond, Reg d, Reg m, bool M, bool N, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n16 = N ? ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result
                     : ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m16 = M ? ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result
                     : ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto result = ir.Mul(n16, m16);
        ir.SetRegister(d, result);
    }
    return true;
}


// Multiply (word by halfword) instructions
bool ArmTranslatorVisitor::arm_SMLAWy(Cond cond, Reg d, Reg a, Reg m, bool M, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || a == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.SignExtendWordToLong(ir.GetRegister(n));
        auto m32 = ir.GetRegister(m);
        if (M)
            m32 = ir.LogicalShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m16 = ir.SignExtendWordToLong(ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32)));
        auto product = ir.LeastSignificantWord(ir.LogicalShiftRight(ir.Mul(n32, m16), ir.Imm8(16)));
        auto result_overflow = ir.AddWithCarry(product, ir.GetRegister(a), ir.Imm1(0));
        ir.SetRegister(d, result_overflow.result);
        ir.OrQFlag(result_overflow.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMULWy(Cond cond, Reg d, Reg m, bool M, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.SignExtendWordToLong(ir.GetRegister(n));
        auto m32 = ir.GetRegister(m);
        if (M)
            m32 = ir.LogicalShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m16 = ir.SignExtendWordToLong(ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32)));
        auto result = ir.LogicalShiftRight(ir.Mul(n32, m16), ir.Imm8(16));
        ir.SetRegister(d, ir.LeastSignificantWord(result));
    }
    return true;
}


// Multiply (Most significant word) instructions
bool ArmTranslatorVisitor::arm_SMMLA(Cond cond, Reg d, Reg a, Reg m, bool R, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC /* no check for a */)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n64 = ir.SignExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.SignExtendWordToLong(ir.GetRegister(m));
        auto a64 = ir.Pack2x32To1x64(ir.Imm32(0), ir.GetRegister(a));
        auto temp = ir.Add(a64, ir.Mul(n64, m64));
        auto result_carry = ir.MostSignificantWord(temp);
        auto result = result_carry.result;
        if (R)
            result = ir.AddWithCarry(result, ir.Imm32(0), result_carry.carry).result;
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMMLS(Cond cond, Reg d, Reg a, Reg m, bool R, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || a == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n64 = ir.SignExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.SignExtendWordToLong(ir.GetRegister(m));
        auto a64 = ir.Pack2x32To1x64(ir.Imm32(0), ir.GetRegister(a));
        auto temp = ir.Sub(a64, ir.Mul(n64, m64));
        auto result_carry = ir.MostSignificantWord(temp);
        auto result = result_carry.result;
        if (R)
            result = ir.AddWithCarry(result, ir.Imm32(0), result_carry.carry).result;
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMMUL(Cond cond, Reg d, Reg m, bool R, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n64 = ir.SignExtendWordToLong(ir.GetRegister(n));
        auto m64 = ir.SignExtendWordToLong(ir.GetRegister(m));
        auto product = ir.Mul(n64, m64);
        auto result_carry = ir.MostSignificantWord(product);
        auto result = result_carry.result;
        if (R)
            result = ir.AddWithCarry(result, ir.Imm32(0), result_carry.carry).result;
        ir.SetRegister(d, result);
    }
    return true;
}


// Multiply (Dual) instructions
bool ArmTranslatorVisitor::arm_SMLAD(Cond cond, Reg d, Reg a, Reg m, bool M, Reg n) {
    if (a == Reg::PC)
        return arm_SMUAD(cond, d, m, M, n);
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto n_hi = ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m_hi = ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        if (M)
            std::swap(m_lo, m_hi);
        auto product_lo = ir.Mul(n_lo, m_lo);
        auto product_hi = ir.Mul(n_hi, m_hi);
        auto addend = ir.GetRegister(a);
        auto result_overflow = ir.AddWithCarry(product_lo, product_hi, ir.Imm1(0));
        ir.OrQFlag(result_overflow.overflow);
        result_overflow = ir.AddWithCarry(result_overflow.result, addend, ir.Imm1(0));
        ir.SetRegister(d, result_overflow.result);
        ir.OrQFlag(result_overflow.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMLALD(Cond cond, Reg dHi, Reg dLo, Reg m, bool M, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto n_hi = ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m_hi = ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        if (M)
            std::swap(m_lo, m_hi);
        auto product_lo = ir.SignExtendWordToLong(ir.Mul(n_lo, m_lo));
        auto product_hi = ir.SignExtendWordToLong(ir.Mul(n_hi, m_hi));
        auto addend = ir.Pack2x32To1x64(ir.GetRegister(dLo), ir.GetRegister(dHi));
        auto result = ir.Add(ir.Add(product_lo, product_hi), addend);
        ir.SetRegister(dLo, ir.LeastSignificantWord(result));
        ir.SetRegister(dHi, ir.MostSignificantWord(result).result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMLSD(Cond cond, Reg d, Reg a, Reg m, bool M, Reg n) {
    if (a == Reg::PC)
        return arm_SMUSD(cond, d, m, M, n);
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto n_hi = ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m_hi = ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        if (M)
            std::swap(m_lo, m_hi);
        auto product_lo = ir.Mul(n_lo, m_lo);
        auto product_hi = ir.Mul(n_hi, m_hi);
        auto addend = ir.GetRegister(a);
        auto result_overflow = ir.AddWithCarry(ir.Sub(product_lo, product_hi), addend, ir.Imm1(0));
        ir.SetRegister(d, result_overflow.result);
        ir.OrQFlag(result_overflow.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMLSLD(Cond cond, Reg dHi, Reg dLo, Reg m, bool M, Reg n) {
    if (dLo == Reg::PC || dHi == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (dLo == dHi)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto n_hi = ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m_hi = ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        if (M)
            std::swap(m_lo, m_hi);
        auto product_lo = ir.SignExtendWordToLong(ir.Mul(n_lo, m_lo));
        auto product_hi = ir.SignExtendWordToLong(ir.Mul(n_hi, m_hi));
        auto addend = ir.Pack2x32To1x64(ir.GetRegister(dLo), ir.GetRegister(dHi));
        auto result = ir.Add(ir.Sub(product_lo, product_hi), addend);
        ir.SetRegister(dLo, ir.LeastSignificantWord(result));
        ir.SetRegister(dHi, ir.MostSignificantWord(result).result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMUAD(Cond cond, Reg d, Reg m, bool M, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto n_hi = ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m_hi = ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        if (M)
            std::swap(m_lo, m_hi);
        auto product_lo = ir.Mul(n_lo, m_lo);
        auto product_hi = ir.Mul(n_hi, m_hi);
        auto result_overflow = ir.AddWithCarry(product_lo, product_hi, ir.Imm1(0));
        ir.SetRegister(d, result_overflow.result);
        ir.OrQFlag(result_overflow.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SMUSD(Cond cond, Reg d, Reg m, bool M, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto n32 = ir.GetRegister(n);
        auto m32 = ir.GetRegister(m);
        auto n_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(n32));
        auto m_lo = ir.SignExtendHalfToWord(ir.LeastSignificantHalf(m32));
        auto n_hi = ir.ArithmeticShiftRight(n32, ir.Imm8(16), ir.Imm1(0)).result;
        auto m_hi = ir.ArithmeticShiftRight(m32, ir.Imm8(16), ir.Imm1(0)).result;
        if (M)
            std::swap(m_lo, m_hi);
        auto product_lo = ir.Mul(n_lo, m_lo);
        auto product_hi = ir.Mul(n_hi, m_hi);
        auto result = ir.Sub(product_lo, product_hi);
        ir.SetRegister(d, result);
    }
    return true;
}

} // namespace Dynarmic::A32
