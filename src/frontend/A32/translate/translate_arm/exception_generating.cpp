/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

#include "dynarmic/A32/config.h"

namespace Dynarmic::A32 {

// BKPT #<imm16>
bool ArmTranslatorVisitor::arm_BKPT(Cond cond, Imm<12> /*imm12*/, Imm<4> /*imm4*/) {
    if (cond != Cond::AL && !options.define_unpredictable_behaviour) {
        return UnpredictableInstruction();
    }
    // UNPREDICTABLE: The instruction executes conditionally.

    if (!ConditionPassed(cond)) {
        return true;
    }

    ir.ExceptionRaised(Exception::Breakpoint);
    ir.SetTerm(IR::Term::CheckHalt{IR::Term::ReturnToDispatch{}});
    return false;
}

// SVC<c> #<imm24>
bool ArmTranslatorVisitor::arm_SVC(Cond cond, Imm<24> imm24) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm24.ZeroExtend();
    ir.PushRSB(ir.current_location.AdvancePC(4));
    ir.BranchWritePC(ir.Imm32(ir.current_location.PC() + 4));
    ir.CallSupervisor(ir.Imm32(imm32));
    ir.SetTerm(IR::Term::CheckHalt{IR::Term::PopRSBHint{}});
    return false;
}

// UDF<c> #<imm16>
bool ArmTranslatorVisitor::arm_UDF() {
    return UndefinedInstruction();
}

} // namespace Dynarmic::A32
