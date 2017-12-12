/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic {
namespace Arm {

bool ArmTranslatorVisitor::arm_CDP(Cond cond, size_t opc1, CoprocReg CRn, CoprocReg CRd, size_t coproc_no, size_t opc2, CoprocReg CRm) {
    if ((coproc_no & 0b1110) == 0b1010)
        return arm_UDF();

    const bool two = cond == Cond::NV;

    // CDP{2} <coproc_no>, #<opc1>, <CRd>, <CRn>, <CRm>, #<opc2>
    if (two || ConditionPassed(cond)) {
        ir.CoprocInternalOperation(coproc_no, two, opc1, CRd, CRn, CRm, opc2);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_LDC(Cond cond, bool p, bool u, bool d, bool w, Reg n, CoprocReg CRd, size_t coproc_no, Imm8 imm8) {
    if (!p && !u && !d && !w)
        return arm_UDF();
    if ((coproc_no & 0b1110) == 0b1010)
        return arm_UDF();

    const bool two = cond == Cond::NV;
    const u32 imm32 = static_cast<u8>(imm8) << 2;
    const bool index = p;
    const bool add = u;
    const bool wback = w;
    const bool has_option = !p && !w && u;

    // LDC{2}{L} <coproc_no>, <CRd>, [<Rn>, #+/-<imm32>]{!}
    // LDC{2}{L} <coproc_no>, <CRd>, [<Rn>], #+/-<imm32>
    // LDC{2}{L} <coproc_no>, <CRd>, [<Rn>], <imm8>
    if (two || ConditionPassed(cond)) {
        auto reg_n = ir.GetRegister(n);
        auto offset_address = add ? ir.Add(reg_n, ir.Imm32(imm32)) : ir.Sub(reg_n, ir.Imm32(imm32));
        auto address = index ? offset_address : reg_n;
        ir.CoprocLoadWords(coproc_no, two, d, CRd, address, has_option, imm8);
        if (wback) {
            ir.SetRegister(n, offset_address);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MCR(Cond cond, size_t opc1, CoprocReg CRn, Reg t, size_t coproc_no, size_t opc2, CoprocReg CRm) {
    if ((coproc_no & 0b1110) == 0b1010)
        return arm_UDF();
    if (t == Reg::PC)
        return UnpredictableInstruction();

    const bool two = cond == Cond::NV;

    // MCR{2} <coproc_no>, #<opc1>, <Rt>, <CRn>, <CRm>, #<opc2>
    if (two || ConditionPassed(cond)) {
        ir.CoprocSendOneWord(coproc_no, two, opc1, CRn, CRm, opc2, ir.GetRegister(t));
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MCRR(Cond cond, Reg t2, Reg t, size_t coproc_no, size_t opc, CoprocReg CRm) {
    if ((coproc_no & 0b1110) == 0b1010)
        return arm_UDF();
    if (t == Reg::PC || t2 == Reg::PC)
        return UnpredictableInstruction();

    const bool two = cond == Cond::NV;

    // MCRR{2} <coproc_no>, #<opc>, <Rt>, <Rt2>, <CRm>
    if (two || ConditionPassed(cond)) {
        ir.CoprocSendTwoWords(coproc_no, two, opc, CRm, ir.GetRegister(t), ir.GetRegister(t2));
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MRC(Cond cond, size_t opc1, CoprocReg CRn, Reg t, size_t coproc_no, size_t opc2, CoprocReg CRm) {
    if ((coproc_no & 0b1110) == 0b1010)
        return arm_UDF();

    const bool two = cond == Cond::NV;

    // MRC{2} <coproc_no>, #<opc1>, <Rt>, <CRn>, <CRm>, #<opc2>
    if (two || ConditionPassed(cond)) {
        auto word = ir.CoprocGetOneWord(coproc_no, two, opc1, CRn, CRm, opc2);
        if (t != Reg::PC) {
            ir.SetRegister(t, word);
        } else {
            auto new_cpsr_nzcv = ir.And(word, ir.Imm32(0xF0000000));
            ir.SetCpsrNZCV(new_cpsr_nzcv);
        }
    }
    return true;
}

bool ArmTranslatorVisitor::arm_MRRC(Cond cond, Reg t2, Reg t, size_t coproc_no, size_t opc, CoprocReg CRm) {
    if ((coproc_no & 0b1110) == 0b1010)
        return arm_UDF();
    if (t == Reg::PC || t2 == Reg::PC || t == t2)
        return UnpredictableInstruction();

    const bool two = cond == Cond::NV;

    // MRRC{2} <coproc_no>, #<opc>, <Rt>, <Rt2>, <CRm>
    if (two || ConditionPassed(cond)) {
        auto two_words = ir.CoprocGetTwoWords(coproc_no, two, opc, CRm);
        ir.SetRegister(t, ir.LeastSignificantWord(two_words));
        ir.SetRegister(t2, ir.MostSignificantWord(two_words).result);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_STC(Cond cond, bool p, bool u, bool d, bool w, Reg n, CoprocReg CRd, size_t coproc_no, Imm8 imm8) {
    if ((coproc_no & 0b1110) == 0b1010)
        return arm_UDF();
    if (!p && !u && !d && !w)
        return arm_UDF();
    if (n == Reg::PC && w)
        return UnpredictableInstruction();

    const bool two = cond == Cond::NV;
    const u32 imm32 = static_cast<u8>(imm8) << 2;
    const bool index = p;
    const bool add = u;
    const bool wback = w;
    const bool has_option = !p && !w && u;

    // STC{2}{L} <coproc>, <CRd>, [<Rn>, #+/-<imm32>]{!}
    // STC{2}{L} <coproc>, <CRd>, [<Rn>], #+/-<imm32>
    // STC{2}{L} <coproc>, <CRd>, [<Rn>], <imm8>
    if (two || ConditionPassed(cond)) {
        auto reg_n = ir.GetRegister(n);
        auto offset_address = add ? ir.Add(reg_n, ir.Imm32(imm32)) : ir.Sub(reg_n, ir.Imm32(imm32));
        auto address = index ? offset_address : reg_n;
        ir.CoprocStoreWords(coproc_no, two, d, CRd, address, has_option, imm8);
        if (wback) {
            ir.SetRegister(n, offset_address);
        }
    }
    return true;
}

} // namespace Arm
} // namespace Dynarmic
