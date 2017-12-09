/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

#include "common/bit_util.h"

namespace Dynarmic {
namespace Arm {

bool ArmTranslatorVisitor::arm_CPS() {
    return InterpretThisInstruction();
}

bool ArmTranslatorVisitor::arm_MRS(Cond cond, Reg d) {
    if (d == Reg::PC)
        return UnpredictableInstruction();
    // MRS <Rd>, APSR
    if (ConditionPassed(cond)) {
        ir.SetRegister(d, ir.GetCpsr());
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MSR_imm(Cond cond, int mask, int rotate, Imm8 imm8) {
    bool write_nzcvq = Common::Bit<1>(mask);
    bool write_g = Common::Bit<0>(mask);
    u32 imm32 = ArmExpandImm(rotate, imm8);
    ASSERT_MSG(write_nzcvq || write_g, "Decode error");
    // MSR <spec_reg>, #<imm32>
    if (ConditionPassed(cond)) {
        if (write_nzcvq) {
            ir.SetCpsrNZCVQ(ir.Imm32(imm32 & 0xF8000000));
        }
        if (write_g) {
            ir.SetGEFlagsCompressed(ir.Imm32(imm32 & 0x000F0000));
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MSR_reg(Cond cond, int mask, Reg n) {
    bool write_nzcvq = Common::Bit<1>(mask);
    bool write_g = Common::Bit<0>(mask);
    if (!write_nzcvq && !write_g)
        return UnpredictableInstruction();
    if (n == Reg::PC)
        return UnpredictableInstruction();
    // MSR <spec_reg>, #<imm32>
    if (ConditionPassed(cond)) {
        auto value = ir.GetRegister(n);
        if (write_nzcvq){
            ir.SetCpsrNZCVQ(ir.And(value, ir.Imm32(0xF8000000)));
        }
        if (write_g){
            ir.SetGEFlagsCompressed(ir.And(value, ir.Imm32(0x000F0000)));
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_RFE() {
    return InterpretThisInstruction();
}

bool ArmTranslatorVisitor::arm_SETEND(bool E) {
    // SETEND {BE,LE}
    ir.SetTerm(IR::Term::LinkBlock{ir.current_location.AdvancePC(4).SetEFlag(E)});
    return false;
}

bool ArmTranslatorVisitor::arm_SRS() {
    return InterpretThisInstruction();
}

} // namespace Arm
} // namespace Dynarmic
