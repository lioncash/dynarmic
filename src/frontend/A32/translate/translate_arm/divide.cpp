/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {
namespace {
using DivideFunction = IR::U32U64 (IREmitter::*)(const IR::U32U64&, const IR::U32U64&);

bool DivideOperation(ArmTranslatorVisitor& v, Cond cond, Reg d, Reg m, Reg n,
                     DivideFunction fn) {
    if (d == Reg::PC || m == Reg::PC || n == Reg::PC) {
        return v.UnpredictableInstruction();
    }

    if (!v.ConditionPassed(cond)) {
        return true;
    }

    const IR::U32 operand1 = v.ir.GetRegister(n);
    const IR::U32 operand2 = v.ir.GetRegister(m);
    const IR::U32 result = (v.ir.*fn)(operand1, operand2);

    v.ir.SetRegister(d, result);
    return true;
}
} // Anonymous namespace

// SDIV<c> <Rd>, <Rn>, <Rm>
bool ArmTranslatorVisitor::arm_SDIV(Cond cond, Reg d, Reg m, Reg n) {
    return DivideOperation(*this, cond, d, m, n, &IREmitter::SignedDiv);
}

// UDIV<c> <Rd>, <Rn>, <Rm>
bool ArmTranslatorVisitor::arm_UDIV(Cond cond, Reg d, Reg m, Reg n) {
    return DivideOperation(*this, cond, d, m, n, &IREmitter::UnsignedDiv);
}

} // namespace Dynarmic::A32
