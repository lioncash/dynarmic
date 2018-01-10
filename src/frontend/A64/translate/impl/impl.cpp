/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/bit_util.h"
#include "frontend/ir/terminal.h"
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::InterpretThisInstruction() {
    ir.SetTerm(IR::Term::Interpret(ir.current_location));
    return false;
}

bool TranslatorVisitor::UnpredictableInstruction() {
    ASSERT_MSG(false, "UNPREDICTABLE");
    return false;
}

bool TranslatorVisitor::ReservedValue() {
    ASSERT_MSG(false, "RESERVEDVALUE");
    return false;
}

bool TranslatorVisitor::UnallocatedEncoding() {
    ASSERT_MSG(false, "UNALLOCATEDENCODING");
    return false;
}

boost::optional<TranslatorVisitor::BitMasks> TranslatorVisitor::DecodeBitMasks(bool immN, Imm<6> imms, Imm<6> immr, bool immediate) {
    int len = Common::HighestSetBit((immN ? 1 << 6 : 0) | (imms.ZeroExtend() ^ 0b111111));
    if (len < 1)
        return boost::none;

    size_t levels = Common::Ones<size_t>(len);

    if (immediate && (imms.ZeroExtend() & levels) == levels)
        return boost::none;

    s32 S = s32(imms.ZeroExtend() & levels);
    s32 R = s32(immr.ZeroExtend() & levels);
    u64 d = u64(S - R) & levels;

    size_t esize = 1 << len;
    u64 welem = Common::Ones<u64>(S + 1);
    u64 telem = Common::Ones<u64>(d + 1);
    u64 wmask = Common::RotateRight(Common::Replicate(welem, esize), R);
    u64 tmask = Common::Replicate(telem, esize);

    return BitMasks{wmask, tmask};
}

IR::U32U64 TranslatorVisitor::I(size_t bitsize, u64 value) {
    switch (bitsize) {
    case 32:
        return ir.Imm32(static_cast<u32>(value));
    case 64:
        return ir.Imm64(value);
    default:
        ASSERT_MSG(false, "Imm - get: Invalid bitsize");
        return {};
    }
}

IR::UAny TranslatorVisitor::X(size_t bitsize, Reg reg) {
    switch (bitsize) {
    case 8:
        return ir.LeastSignificantByte(ir.GetW(reg));
    case 16:
        return ir.LeastSignificantHalf(ir.GetW(reg));
    case 32:
        return ir.GetW(reg);
    case 64:
        return ir.GetX(reg);
    default:
        ASSERT_MSG(false, "X - get: Invalid bitsize");
        return {};
    }
}

void TranslatorVisitor::X(size_t bitsize, Reg reg, IR::U32U64 value) {
    switch (bitsize) {
    case 32:
        ir.SetW(reg, value);
        return;
    case 64:
        ir.SetX(reg, value);
        return;
    default:
        ASSERT_MSG(false, "X - set: Invalid bitsize");
    }
}

IR::U32U64 TranslatorVisitor::SP(size_t bitsize) {
    switch (bitsize) {
    case 32:
        return ir.LeastSignificantWord(ir.GetSP());
    case 64:
        return ir.GetSP();
    default:
        ASSERT_MSG(false, "SP - get : Invalid bitsize");
        return {};
    }
}

void TranslatorVisitor::SP(size_t bitsize, IR::U32U64 value) {
    switch (bitsize) {
    case 32:
        ir.SetSP(ir.ZeroExtendWordToLong(value));
        break;
    case 64:
        ir.SetSP(value);
        break;
    default:
        ASSERT_MSG(false, "SP -  : Invalid bitsize");
    }
}

IR::UAny TranslatorVisitor::Mem(IR::U64 address, size_t bytesize, AccType /*acctype*/) {
    switch (bytesize) {
    case 1:
        return ir.ReadMemory8(address);
    case 2:
        return ir.ReadMemory16(address);
    case 4:
        return ir.ReadMemory32(address);
    case 8:
        return ir.ReadMemory64(address);
    default:
        ASSERT_MSG(false, "Invalid bytesize parameter %zu", bytesize);
        return {};
    }
}

void TranslatorVisitor::Mem(IR::U64 address, size_t bytesize, AccType /*acctype*/, IR::UAny value) {
    switch (bytesize) {
    case 1:
        ir.WriteMemory8(address, value);
        return;
    case 2:
        ir.WriteMemory16(address, value);
        return;
    case 4:
        ir.WriteMemory32(address, value);
        return;
    case 8:
        ir.WriteMemory64(address, value);
        return;
    default:
        ASSERT_MSG(false, "Invalid bytesize parameter %zu", bytesize);
        return;
    }
}

IR::U32U64 TranslatorVisitor::SignExtend(IR::UAny value, size_t to_size) {
    switch (to_size) {
    case 32:
        return ir.SignExtendToWord(value);
    case 64:
        return ir.SignExtendToLong(value);
    default:
        ASSERT_MSG(false, "Invalid size parameter %zu", to_size);
        return {};
    }
}

IR::U32U64 TranslatorVisitor::ZeroExtend(IR::UAny value, size_t to_size) {
    switch (to_size) {
    case 32:
        return ir.ZeroExtendToWord(value);
    case 64:
        return ir.ZeroExtendToLong(value);
    default:
        ASSERT_MSG(false, "Invalid size parameter %zu", to_size);
        return {};
    }
}

IR::U32U64 TranslatorVisitor::ShiftReg(size_t bitsize, Reg reg, Imm<2> shift, IR::U8 amount) {
    IR::U32U64 result = X(bitsize, reg);
    switch (shift.ZeroExtend()) {
    case 0b00:
        return ir.LogicalShiftLeft(result, amount);
    case 0b01:
        return ir.LogicalShiftRight(result, amount);
    case 0b10:
        return ir.ArithmeticShiftRight(result, amount);
    case 0b11:
        return ir.RotateRight(result, amount);
    }
    ASSERT_MSG(false, "Unreachable");
    return {};
}

IR::U32U64 TranslatorVisitor::ExtendReg(size_t bitsize, Reg reg, Imm<3> option, u8 shift) {
    ASSERT(shift <= 4);
    ASSERT(bitsize == 32 || bitsize == 64);
    IR::UAny val = X(bitsize, reg);
    size_t len;
    IR::U32U64 extended;
    bool signed_extend;

    switch (option.ZeroExtend()) {
    case 0b000: { // UXTB
        val = ir.LeastSignificantByte(val);
        len = 8;
        signed_extend = false;
        break;
    }
    case 0b001: { // UXTH
        val = ir.LeastSignificantHalf(val);
        len = 16;
        signed_extend = false;
        break;
    }
    case 0b010: { // UXTW
        if (bitsize != 32) {
            val = ir.LeastSignificantWord(val);
        }
        len = 32;
        signed_extend = false;
        break;
    }
    case 0b011: { // UXTX
        len = 64;
        signed_extend = false;
        break;
    }
    case 0b100: { // SXTB
        val = ir.LeastSignificantByte(val);
        len = 8;
        signed_extend = true;
        break;
    }
    case 0b101: { // SXTH
        val = ir.LeastSignificantHalf(val);
        len = 16;
        signed_extend = true;
        break;
    }
    case 0b110: { // SXTW
        if (bitsize != 32) {
            val = ir.LeastSignificantWord(val);
        }
        len = 32;
        signed_extend = true;
        break;
    }
    case 0b111: { // SXTX
        len = 64;
        signed_extend = true;
        break;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }

    if (len < bitsize) {
        if (bitsize == 32) {
            extended = signed_extend ? ir.SignExtendToWord(val) : ir.ZeroExtendToWord(val);
        } else {
            extended = signed_extend ? ir.SignExtendToLong(val) : ir.ZeroExtendToLong(val);
        }
    } else {
        extended = val;
    }

    return ir.LogicalShiftLeft(extended, ir.Imm8(shift));
}

} // namespace A64
} // namespace Dynarmic
