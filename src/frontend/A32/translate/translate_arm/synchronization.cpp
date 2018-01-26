/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

bool ArmTranslatorVisitor::arm_CLREX() {
    // CLREX
    ir.ClearExclusive();
    return true;
}

bool ArmTranslatorVisitor::arm_LDREX(Cond cond, Reg n, Reg d) {
    if (d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();
    // LDREX <Rd>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        ir.SetExclusive(address, 4);
        ir.SetRegister(d, ir.ReadMemory32(address));
    }
    return true;
}

bool ArmTranslatorVisitor::arm_LDREXB(Cond cond, Reg n, Reg d) {
    if (d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();
    // LDREXB <Rd>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        ir.SetExclusive(address, 1);
        ir.SetRegister(d, ir.ZeroExtendByteToWord(ir.ReadMemory8(address)));
    }
    return true;
}

bool ArmTranslatorVisitor::arm_LDREXD(Cond cond, Reg n, Reg d) {
    if (d == Reg::LR || d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();
    // LDREXD <Rd>, <Rd1>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        ir.SetExclusive(address, 8);
        // DO NOT SWAP hi AND lo IN BIG ENDIAN MODE, THIS IS CORRECT BEHAVIOUR
        auto lo = ir.ReadMemory32(address);
        ir.SetRegister(d, lo);
        auto hi = ir.ReadMemory32(ir.Add(address, ir.Imm32(4)));
        ir.SetRegister(d+1, hi);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_LDREXH(Cond cond, Reg n, Reg d) {
    if (d == Reg::PC || n == Reg::PC)
        return UnpredictableInstruction();
    // LDREXH <Rd>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        ir.SetExclusive(address, 2);
        ir.SetRegister(d, ir.ZeroExtendHalfToWord(ir.ReadMemory16(address)));
    }
    return true;
}

bool ArmTranslatorVisitor::arm_STREX(Cond cond, Reg n, Reg d, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (d == n || d == m)
        return UnpredictableInstruction();
    // STREX <Rd>, <Rm>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        auto value = ir.GetRegister(m);
        auto passed = ir.ExclusiveWriteMemory32(address, value);
        ir.SetRegister(d, passed);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_STREXB(Cond cond, Reg n, Reg d, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (d == n || d == m)
        return UnpredictableInstruction();
    // STREXB <Rd>, <Rm>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        auto value = ir.LeastSignificantByte(ir.GetRegister(m));
        auto passed = ir.ExclusiveWriteMemory8(address, value);
        ir.SetRegister(d, passed);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_STREXD(Cond cond, Reg n, Reg d, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::LR || static_cast<size_t>(m) % 2 == 1)
        return UnpredictableInstruction();
    if (d == n || d == m || d == m+1)
        return UnpredictableInstruction();
    Reg m2 = m + 1;
    // STREXD <Rd>, <Rm>, <Rm2>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        auto value_lo = ir.GetRegister(m);
        auto value_hi = ir.GetRegister(m2);
        auto passed = ir.ExclusiveWriteMemory64(address, value_lo, value_hi);
        ir.SetRegister(d, passed);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_STREXH(Cond cond, Reg n, Reg d, Reg m) {
    if (n == Reg::PC || d == Reg::PC || m == Reg::PC)
        return UnpredictableInstruction();
    if (d == n || d == m)
        return UnpredictableInstruction();
    // STREXH <Rd>, <Rm>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto address = ir.GetRegister(n);
        auto value = ir.LeastSignificantHalf(ir.GetRegister(m));
        auto passed = ir.ExclusiveWriteMemory16(address, value);
        ir.SetRegister(d, passed);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SWP(Cond cond, Reg n, Reg t, Reg t2) {
    if (t == Reg::PC || t2 == Reg::PC || n == Reg::PC || n == t || n == t2)
        return UnpredictableInstruction();
    // TODO: UNDEFINED if current mode is Hypervisor
    // SWP <Rt>, <Rt2>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto data = ir.ReadMemory32(ir.GetRegister(n));
        ir.WriteMemory32(ir.GetRegister(n), ir.GetRegister(t2));
        // TODO: Alignment check
        ir.SetRegister(t, data);
    }
    return true;
}

bool ArmTranslatorVisitor::arm_SWPB(Cond cond, Reg n, Reg t, Reg t2) {
    if (t == Reg::PC || t2 == Reg::PC || n == Reg::PC || n == t || n == t2)
        return UnpredictableInstruction();
    // TODO: UNDEFINED if current mode is Hypervisor
    // SWPB <Rt>, <Rt2>, [<Rn>]
    if (ConditionPassed(cond)) {
        auto data = ir.ReadMemory8(ir.GetRegister(n));
        ir.WriteMemory8(ir.GetRegister(n), ir.LeastSignificantByte(ir.GetRegister(t2)));
        // TODO: Alignment check
        ir.SetRegister(t, ir.ZeroExtendByteToWord(data));
    }
    return true;
}


} // namespace Dynarmic::A32
