/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

// Register encodings used by MRS and MSR.
enum class SystemRegisterEncoding : u32 {
    // Counter-timer Frequency register
    CNTFRQ_EL0 = 0b11'011'1110'0000'000,
    // Counter-timer Physical Count register
    CNTPCT_EL0 = 0b11'011'1110'0000'001,
    // Cache Type Register
    CTR_EL0 = 0b11'011'0000'0000'001,
    // Data Cache Zero ID register
    DCZID_EL0 = 0b11'011'0000'0000'111,
    // Floating-point Control Register
    FPCR = 0b11'011'0100'0100'000,
    // Floating-point Status Register
    FPSR = 0b11'011'0100'0100'001,
    // Read/Write Software Thread ID Register
    TPIDR_EL0 = 0b11'011'1101'0000'010,
    // Read-Only Software Thread ID Register
    TPIDRRO_EL0 = 0b11'011'1101'0000'011,
};

bool TranslatorVisitor::HINT([[maybe_unused]] Imm<4> CRm, [[maybe_unused]] Imm<3> op2) {
    return true;
}

bool TranslatorVisitor::NOP() {
    return true;
}

bool TranslatorVisitor::YIELD() {
    return RaiseException(Exception::Yield);
}

bool TranslatorVisitor::WFE() {
    return RaiseException(Exception::WaitForEvent);
}

bool TranslatorVisitor::WFI() {
    return RaiseException(Exception::WaitForInterrupt);
}

bool TranslatorVisitor::SEV() {
    return RaiseException(Exception::SendEvent);
}

bool TranslatorVisitor::SEVL() {
    return RaiseException(Exception::SendEventLocal);
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

bool TranslatorVisitor::MSR_reg(Imm<1> o0, Imm<3> op1, Imm<4> CRn, Imm<4> CRm, Imm<3> op2, Reg Rt) {
    const auto sys_reg = concatenate(Imm<1>{1}, o0, op1, CRn, CRm, op2).ZeroExtend<SystemRegisterEncoding>();
    switch (sys_reg) {
    case SystemRegisterEncoding::FPCR:
        ir.SetFPCR(X(32, Rt));
        ir.SetPC(ir.Imm64(ir.current_location->PC() + 4));
        ir.SetTerm(IR::Term::ReturnToDispatch{});
        return false;
    case SystemRegisterEncoding::FPSR:
        ir.SetFPSR(X(32, Rt));
        return true;
    case SystemRegisterEncoding::TPIDR_EL0:
        ir.SetTPIDR(X(64, Rt));
        return true;
    default:
        break;
    }
    return InterpretThisInstruction();
}

bool TranslatorVisitor::MRS(Imm<1> o0, Imm<3> op1, Imm<4> CRn, Imm<4> CRm, Imm<3> op2, Reg Rt) {
    const auto sys_reg = concatenate(Imm<1>{1}, o0, op1, CRn, CRm, op2).ZeroExtend<SystemRegisterEncoding>();
    switch (sys_reg) {
    case SystemRegisterEncoding::CNTFRQ_EL0:
        X(32, Rt, ir.GetCNTFRQ());
        return true;
    case SystemRegisterEncoding::CNTPCT_EL0:
        // HACK: Ensure that this is the first instruction in the block it's emitted in, so the cycle count is most up-to-date.
        if (!ir.block.empty()) {
            ir.block.CycleCount()--;
            ir.SetTerm(IR::Term::LinkBlock{*ir.current_location});
            return false;
        }
        X(64, Rt, ir.GetCNTPCT());
        return true;
    case SystemRegisterEncoding::CTR_EL0:
        X(32, Rt, ir.GetCTR());
        return true;
    case SystemRegisterEncoding::DCZID_EL0:
        X(32, Rt, ir.GetDCZID());
        return true;
    case SystemRegisterEncoding::FPCR:
        X(32, Rt, ir.GetFPCR());
        return true;
    case SystemRegisterEncoding::FPSR:
        X(32, Rt, ir.GetFPSR());
        return true;
    case SystemRegisterEncoding::TPIDR_EL0:
        X(64, Rt, ir.GetTPIDR());
        return true;
    case SystemRegisterEncoding::TPIDRRO_EL0:
        X(64, Rt, ir.GetTPIDRRO());
        return true;

    }
    return InterpretThisInstruction();
}

} // namespace Dynarmic::A64
