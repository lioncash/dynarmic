/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

// Parallel Add/Subtract (Modulo arithmetic) instructions
bool ArmTranslatorVisitor::arm_SADD8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedAddS8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SADD16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedAddS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SASX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedAddSubS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SSAX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSubAddS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SSUB8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSubS8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SSUB16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSubS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UADD8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedAddU8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UADD16(Cond cond, Reg n, Reg d, Reg m) {
  if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
      return UnpredictableInstruction();
  if (ConditionPassed(cond)) {
      auto result = ir.PackedAddU16(ir.GetRegister(n), ir.GetRegister(m));
      ir.SetRegister(d, result.result);
      ir.SetGEFlags(result.ge);
  }
  return true;
}

bool ArmTranslatorVisitor::arm_UASX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedAddSubU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_USAX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSubAddU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_USAD8(Cond cond, Reg d, Reg m, Reg n) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedAbsDiffSumS8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_USADA8(Cond cond, Reg d, Reg a, Reg m, Reg n){
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto tmp = ir.PackedAbsDiffSumS8(ir.GetRegister(n), ir.GetRegister(m));
        auto result = ir.AddWithCarry(ir.GetRegister(a), tmp, ir.Imm1(0));
        ir.SetRegister(d, result.result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_USUB8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSubU8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_USUB16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSubU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result.result);
        ir.SetGEFlags(result.ge);
    }
    return true;
}

// Parallel Add/Subtract (Saturating) instructions
bool ArmTranslatorVisitor::arm_QADD8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedAddS8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_QADD16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedAddS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_QSUB8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedSubS8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_QSUB16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedSubS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UQADD8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedAddU8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UQADD16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedAddU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UQSUB8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedSubU8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UQSUB16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedSaturatedSubU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

// Parallel Add/Subtract (Halving) instructions
bool ArmTranslatorVisitor::arm_SHADD8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingAddS8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SHADD16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingAddS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SHASX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingAddSubS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SHSAX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingSubAddS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SHSUB8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingSubS8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SHSUB16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingSubS16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UHADD8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingAddU8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UHADD16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingAddU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UHASX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingAddSubU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UHSAX(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingSubAddU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UHSUB8(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingSubU8(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UHSUB16(Cond cond, Reg n, Reg d, Reg m) {
    if (d == Reg::PC || n == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (ConditionPassed(cond)) {
        auto result = ir.PackedHalvingSubU16(ir.GetRegister(n), ir.GetRegister(m));
        ir.SetRegister(d, result);
    }
    return true;
}

} // namespace Dynarmic::A32
