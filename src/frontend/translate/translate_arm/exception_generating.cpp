/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic {
namespace Arm {

bool ArmTranslatorVisitor::arm_BKPT(Cond /*cond*/, Imm12 /*imm12*/, Imm4 /*imm4*/) {
    return InterpretThisInstruction();
}

bool ArmTranslatorVisitor::arm_SVC(Cond cond, Imm24 imm24) {
    u32 imm32 = imm24;
    // SVC<c> #<imm24>
    if (ConditionPassed(cond)) {
        ir.PushRSB(ir.current_location.AdvancePC(4));
        ir.BranchWritePC(ir.Imm32(ir.current_location.PC() + 4));
        ir.CallSupervisor(ir.Imm32(imm32));
        ir.SetTerm(IR::Term::CheckHalt{IR::Term::PopRSBHint{}});
        return false;
    }
    return true;
}

bool ArmTranslatorVisitor::arm_UDF() {
    return InterpretThisInstruction();
}

} // namespace Arm
} // namespace Dynarmic
