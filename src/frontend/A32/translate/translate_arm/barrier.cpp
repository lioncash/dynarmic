/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

bool ArmTranslatorVisitor::arm_DMB([[maybe_unused]] Imm<4> option) {
    ir.DataMemoryBarrier();
    return true;
}

bool ArmTranslatorVisitor::arm_DSB([[maybe_unused]] Imm<4> option) {
    ir.DataSynchronizationBarrier();
    return true;
}

bool ArmTranslatorVisitor::arm_ISB([[maybe_unused]] Imm<4> option) {
    ir.InstructionSynchronizationBarrier();
    ir.BranchWritePC(ir.Imm32(ir.current_location.PC() + 4));
    ir.SetTerm(IR::Term::ReturnToDispatch{});
    return false;
}

} // namespace Dynarmic::A32
