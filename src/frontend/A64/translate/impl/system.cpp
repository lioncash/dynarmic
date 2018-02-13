/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::HINT([[maybe_unused]] Imm<4> CRm, [[maybe_unused]] Imm<3> op2) {
    return true;
}

bool TranslatorVisitor::NOP() {
    return true;
}

bool TranslatorVisitor::YIELD() {
    return true;
}

bool TranslatorVisitor::WFE() {
    return true;
}

bool TranslatorVisitor::WFI() {
    return true;
}

bool TranslatorVisitor::SEV() {
    return true;
}

bool TranslatorVisitor::SEVL() {
    return true;
}

bool TranslatorVisitor::CLREX(Imm<4> /*CRm*/) {
    ir.ClearExclusive();
    return true;
}

bool TranslatorVisitor::DSB(Imm<4> /*CRm*/) {
    ir.DataSynchronizationBarrier();
    return true;
}

bool TranslatorVisitor::DMB(Imm<4> /*CRm*/) {
    ir.DataMemoryBarrier();
    return true;
}

bool TranslatorVisitor::MRS(Imm<1> o0, Imm<3> op1, Imm<4> CRn, Imm<4> CRm, Imm<3> op2, Reg Rt) {
    const size_t sys_reg = concatenate(Imm<1>{1}, o0, op1, CRn, CRm, op2).ZeroExtend<size_t>();
    switch (sys_reg) {
    case 0b11'011'1101'0000'011: // TPIDRRO_EL0
        X(64, Rt, ir.GetTPIDRRO());
        return true;
    case 0b11'011'0000'0000'111: // DCZID_EL0
        X(32, Rt, ir.GetDCZID());
        return true;
    }
    return InterpretThisInstruction();
}

} // namespace Dynarmic::A64
