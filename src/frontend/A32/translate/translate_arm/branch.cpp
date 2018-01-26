/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/bit_util.h"

#include "translate_arm.h"

namespace Dynarmic::A32 {

bool ArmTranslatorVisitor::arm_B(Cond cond, Imm24 imm24) {
    u32 imm32 = Common::SignExtend<26, u32>(imm24 << 2) + 8;
    // B <label>
    if (ConditionPassed(cond)) {
        auto new_location = ir.current_location.AdvancePC(imm32);
        ir.SetTerm(IR::Term::LinkBlock{ new_location });
        return false;
    }
    return true;
}

bool ArmTranslatorVisitor::arm_BL(Cond cond, Imm24 imm24) {
    u32 imm32 = Common::SignExtend<26, u32>(imm24 << 2) + 8;
    // BL <label>
    if (ConditionPassed(cond)) {
        ir.PushRSB(ir.current_location.AdvancePC(4));
        ir.SetRegister(Reg::LR, ir.Imm32(ir.current_location.PC() + 4));
        auto new_location = ir.current_location.AdvancePC(imm32);
        ir.SetTerm(IR::Term::LinkBlock{ new_location });
        return false;
    }
    return true;
}

bool ArmTranslatorVisitor::arm_BLX_imm(bool H, Imm24 imm24) {
    u32 imm32 = Common::SignExtend<26, u32>((imm24 << 2)) + (H ? 2 : 0) + 8;
    // BLX <label>
    ir.PushRSB(ir.current_location.AdvancePC(4));
    ir.SetRegister(Reg::LR, ir.Imm32(ir.current_location.PC() + 4));
    auto new_location = ir.current_location.AdvancePC(imm32).SetTFlag(true);
    ir.SetTerm(IR::Term::LinkBlock{ new_location });
    return false;
}

bool ArmTranslatorVisitor::arm_BLX_reg(Cond cond, Reg m) {
    if (m == Reg::PC)
        return UnpredictableInstruction();
    // BLX <Rm>
    if (ConditionPassed(cond)) {
        ir.PushRSB(ir.current_location.AdvancePC(4));
        ir.BXWritePC(ir.GetRegister(m));
        ir.SetRegister(Reg::LR, ir.Imm32(ir.current_location.PC() + 4));
        ir.SetTerm(IR::Term::ReturnToDispatch{});
        return false;
    }
    return true;
}

bool ArmTranslatorVisitor::arm_BX(Cond cond, Reg m) {
    // BX <Rm>
    if (ConditionPassed(cond)) {
        ir.BXWritePC(ir.GetRegister(m));
        if (m == Reg::R14)
            ir.SetTerm(IR::Term::PopRSBHint{});
        else
            ir.SetTerm(IR::Term::ReturnToDispatch{});
        return false;
    }
    return true;
}

bool ArmTranslatorVisitor::arm_BXJ(Cond cond, Reg m) {
    // Jazelle not supported
    return arm_BX(cond, m);
}

} // namespace Dynarmic::A32
