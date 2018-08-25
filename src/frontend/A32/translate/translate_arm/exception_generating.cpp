/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

#include "dynarmic/A32/config.h"

namespace Dynarmic::A32 {

bool ArmTranslatorVisitor::arm_BKPT(Cond cond, Imm12 /*imm12*/, Imm4 /*imm4*/) {
    if (cond != Cond::AL && !options.define_unpredictable_behaviour) {
        return UnpredictableInstruction();
    }
    // UNPREDICTABLE: The instruction executes conditionally.

    if (ConditionPassed(cond)) {
        ir.ExceptionRaised(Exception::Breakpoint);
        ir.SetTerm(IR::Term::CheckHalt{IR::Term::ReturnToDispatch{}});
        return false;
    }
    return true;
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
    return UndefinedInstruction();
}

} // namespace Dynarmic::A32
