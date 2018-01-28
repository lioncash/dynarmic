/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

bool ArmTranslatorVisitor::arm_ADC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    // ADC{S}<c> <Rd>, <Rn>, #<imm>
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.AddWithCarry(ir.GetRegister(n), ir.Imm32(imm32), ir.GetCFlag());

        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }

        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ADC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.AddWithCarry(ir.GetRegister(n), shifted.result, ir.GetCFlag());
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ADC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.AddWithCarry(ir.GetRegister(n), shifted.result, ir.GetCFlag());
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ADD_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.AddWithCarry(ir.GetRegister(n), ir.Imm32(imm32), ir.Imm1(0));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ADD_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.AddWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(0));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ADD_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.AddWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(0));
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_AND_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.And(ir.GetRegister(n), ir.Imm32(imm_carry.imm32));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(imm_carry.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_AND_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = ir.And(ir.GetRegister(n), shifted.result);
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_AND_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.And(ir.GetRegister(n), shifted.result);
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_BIC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.And(ir.GetRegister(n), ir.Not(ir.Imm32(imm_carry.imm32)));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(imm_carry.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_BIC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = ir.And(ir.GetRegister(n), ir.Not(shifted.result));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_BIC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.And(ir.GetRegister(n), ir.Not(shifted.result));
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_CMN_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.AddWithCarry(ir.GetRegister(n), ir.Imm32(imm32), ir.Imm1(0));
        ir.SetNFlag(ir.MostSignificantBit(result.result));
        ir.SetZFlag(ir.IsZero(result.result));
        ir.SetCFlag(result.carry);
        ir.SetVFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_CMN_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.AddWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(0));
        ir.SetNFlag(ir.MostSignificantBit(result.result));
        ir.SetZFlag(ir.IsZero(result.result));
        ir.SetCFlag(result.carry);
        ir.SetVFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_CMN_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.AddWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(0));
        ir.SetNFlag(ir.MostSignificantBit(result.result));
        ir.SetZFlag(ir.IsZero(result.result));
        ir.SetCFlag(result.carry);
        ir.SetVFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_CMP_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
    // CMP<c> <Rn>, #<imm>
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.SubWithCarry(ir.GetRegister(n), ir.Imm32(imm32), ir.Imm1(1));
        ir.SetNFlag(ir.MostSignificantBit(result.result));
        ir.SetZFlag(ir.IsZero(result.result));
        ir.SetCFlag(result.carry);
        ir.SetVFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_CMP_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.SubWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(1));
        ir.SetNFlag(ir.MostSignificantBit(result.result));
        ir.SetZFlag(ir.IsZero(result.result));
        ir.SetCFlag(result.carry);
        ir.SetVFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_CMP_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.SubWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(1));
        ir.SetNFlag(ir.MostSignificantBit(result.result));
        ir.SetZFlag(ir.IsZero(result.result));
        ir.SetCFlag(result.carry);
        ir.SetVFlag(result.overflow);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_EOR_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.Eor(ir.GetRegister(n), ir.Imm32(imm_carry.imm32));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(imm_carry.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_EOR_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = ir.Eor(ir.GetRegister(n), shifted.result);
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_EOR_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.Eor(ir.GetRegister(n), shifted.result);
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MOV_imm(Cond cond, bool S, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.Imm32(imm_carry.imm32);
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(imm_carry.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MOV_reg(Cond cond, bool S, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = shifted.result;
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MOV_rsr(Cond cond, bool S, Reg d, Reg s, ShiftType shift, Reg m) {
    if (d == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = shifted.result;
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MVN_imm(Cond cond, bool S, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.Not(ir.Imm32(imm_carry.imm32));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(imm_carry.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MVN_reg(Cond cond, bool S, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = ir.Not(shifted.result);
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MVN_rsr(Cond cond, bool S, Reg d, Reg s, ShiftType shift, Reg m) {
    if (d == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.Not(shifted.result);
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ORR_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.Or(ir.GetRegister(n), ir.Imm32(imm_carry.imm32));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(imm_carry.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ORR_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = ir.Or(ir.GetRegister(n), shifted.result);
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_ORR_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC || d == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.Or(ir.GetRegister(n), shifted.result);
        ir.SetRegister(d, result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result));
            ir.SetZFlag(ir.IsZero(result));
            ir.SetCFlag(shifted.carry);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_RSB_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.SubWithCarry(ir.Imm32(imm32), ir.GetRegister(n), ir.Imm1(1));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_RSB_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.SubWithCarry(shifted.result, ir.GetRegister(n), ir.Imm1(1));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_RSB_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC || d == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.SubWithCarry(shifted.result, ir.GetRegister(n), ir.Imm1(1));
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_RSC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.SubWithCarry(ir.Imm32(imm32), ir.GetRegister(n), ir.GetCFlag());
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_RSC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.SubWithCarry(shifted.result, ir.GetRegister(n), ir.GetCFlag());
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_RSC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC || d == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.SubWithCarry(shifted.result, ir.GetRegister(n), ir.GetCFlag());
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SBC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.SubWithCarry(ir.GetRegister(n), ir.Imm32(imm32), ir.GetCFlag());
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SBC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.SubWithCarry(ir.GetRegister(n), shifted.result, ir.GetCFlag());
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SBC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC || d == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.SubWithCarry(ir.GetRegister(n), shifted.result, ir.GetCFlag());
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SUB_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        u32 imm32 = ArmExpandImm(rotate, imm8);
        auto result = ir.SubWithCarry(ir.GetRegister(n), ir.Imm32(imm32), ir.Imm1(1));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SUB_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, ir.GetCFlag());
        auto result = ir.SubWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(1));
        if (d == Reg::PC) {
            if (S) return UnpredictableInstruction(); // This is UNPREDICTABLE when in user-mode.
            ir.ALUWritePC(result.result);
            ir.SetTerm(IR::Term::ReturnToDispatch{});
            return false;
        }
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SUB_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC || d == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.SubWithCarry(ir.GetRegister(n), shifted.result, ir.Imm1(1));
        ir.SetRegister(d, result.result);
        if (S) {
            ir.SetNFlag(ir.MostSignificantBit(result.result));
            ir.SetZFlag(ir.IsZero(result.result));
            ir.SetCFlag(result.carry);
            ir.SetVFlag(result.overflow);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_TEQ_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.Eor(ir.GetRegister(n), ir.Imm32(imm_carry.imm32));
        ir.SetNFlag(ir.MostSignificantBit(result));
        ir.SetZFlag(ir.IsZero(result));
        ir.SetCFlag(imm_carry.carry);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_TEQ_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = ir.Eor(ir.GetRegister(n), shifted.result);
        ir.SetNFlag(ir.MostSignificantBit(result));
        ir.SetZFlag(ir.IsZero(result));
        ir.SetCFlag(shifted.carry);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_TEQ_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.Eor(ir.GetRegister(n), shifted.result);
        ir.SetNFlag(ir.MostSignificantBit(result));
        ir.SetZFlag(ir.IsZero(result));
        ir.SetCFlag(shifted.carry);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_TST_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
    if (ConditionPassed(cond)) {
        auto imm_carry = ArmExpandImm_C(rotate, imm8, ir.GetCFlag());
        auto result = ir.And(ir.GetRegister(n), ir.Imm32(imm_carry.imm32));
        ir.SetNFlag(ir.MostSignificantBit(result));
        ir.SetZFlag(ir.IsZero(result));
        ir.SetCFlag(imm_carry.carry);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_TST_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
    if (ConditionPassed(cond)) {
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitImmShift(ir.GetRegister(m), shift, imm5, carry_in);
        auto result = ir.And(ir.GetRegister(n), shifted.result);
        ir.SetNFlag(ir.MostSignificantBit(result));
        ir.SetZFlag(ir.IsZero(result));
        ir.SetCFlag(shifted.carry);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_TST_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
    if (n == Reg::PC || m == Reg::PC || s == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto shift_n = ir.LeastSignificantByte(ir.GetRegister(s));
        auto carry_in = ir.GetCFlag();
        auto shifted = EmitRegShift(ir.GetRegister(m), shift, shift_n, carry_in);
        auto result = ir.And(ir.GetRegister(n), shifted.result);
        ir.SetNFlag(ir.MostSignificantBit(result));
        ir.SetZFlag(ir.IsZero(result));
        ir.SetCFlag(shifted.carry);
    }
    return true;
}

} // namespace Dynarmic::A32
