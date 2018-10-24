/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/bit_util.h"
#include "frontend/ir/terminal.h"
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::InterpretThisInstruction() {
    ir.SetTerm(IR::Term::Interpret(*ir.current_location));
    return false;
}

bool TranslatorVisitor::UnpredictableInstruction() {
    return RaiseException(Exception::UnpredictableInstruction);
}

bool TranslatorVisitor::DecodeError() {
    UNREACHABLE();
    return false;
}

bool TranslatorVisitor::ReservedValue() {
    return RaiseException(Exception::ReservedValue);
}

bool TranslatorVisitor::UnallocatedEncoding() {
    return RaiseException(Exception::UnallocatedEncoding);
}

bool TranslatorVisitor::RaiseException(Exception exception) {
    ir.SetPC(ir.Imm64(ir.current_location->PC() + 4));
    ir.ExceptionRaised(exception);
    ir.SetTerm(IR::Term::CheckHalt{IR::Term::ReturnToDispatch{}});
    return false;
}

std::optional<TranslatorVisitor::BitMasks> TranslatorVisitor::DecodeBitMasks(bool immN, Imm<6> imms, Imm<6> immr, bool immediate) {
    int len = Common::HighestSetBit((immN ? 1 << 6 : 0) | (imms.ZeroExtend() ^ 0b111111));
    if (len < 1)
        return std::nullopt;

    size_t levels = Common::Ones<size_t>(len);

    if (immediate && (imms.ZeroExtend() & levels) == levels)
        return std::nullopt;

    s32 S = s32(imms.ZeroExtend() & levels);
    s32 R = s32(immr.ZeroExtend() & levels);
    u64 d = u64(S - R) & levels;

    size_t esize = static_cast<size_t>(1) << len;
    u64 welem = Common::Ones<u64>(S + 1);
    u64 telem = Common::Ones<u64>(d + 1);
    u64 wmask = Common::RotateRight(Common::Replicate(welem, esize), R);
    u64 tmask = Common::Replicate(telem, esize);

    return BitMasks{wmask, tmask};
}

u64 TranslatorVisitor::AdvSIMDExpandImm(bool op, Imm<4> cmode, Imm<8> imm8) {
    switch (cmode.Bits<1, 3>()) {
    case 0b000:
        return Common::Replicate<u64>(imm8.ZeroExtend<u64>(), 32);
    case 0b001:
        return Common::Replicate<u64>(imm8.ZeroExtend<u64>() << 8, 32);
    case 0b010:
        return Common::Replicate<u64>(imm8.ZeroExtend<u64>() << 16, 32);
    case 0b011:
        return Common::Replicate<u64>(imm8.ZeroExtend<u64>() << 24, 32);
    case 0b100:
        return Common::Replicate<u64>(imm8.ZeroExtend<u64>(), 16);
    case 0b101:
        return Common::Replicate<u64>(imm8.ZeroExtend<u64>() << 8, 16);
    case 0b110:
        if (!cmode.Bit<0>()) {
            return Common::Replicate<u64>((imm8.ZeroExtend<u64>() << 8) | Common::Ones<u64>(8), 32);
        }
        return Common::Replicate<u64>((imm8.ZeroExtend<u64>() << 16) | Common::Ones<u64>(16), 32);
    case 0b111:
        if (!cmode.Bit<0>() && !op) {
            return Common::Replicate<u64>(imm8.ZeroExtend<u64>(), 8);
        }
        if (!cmode.Bit<0>() && op) {
            u64 result = 0;
            result |= imm8.Bit<0>() ? Common::Ones<u64>(8) << (0 * 8) : 0;
            result |= imm8.Bit<1>() ? Common::Ones<u64>(8) << (1 * 8) : 0;
            result |= imm8.Bit<2>() ? Common::Ones<u64>(8) << (2 * 8) : 0;
            result |= imm8.Bit<3>() ? Common::Ones<u64>(8) << (3 * 8) : 0;
            result |= imm8.Bit<4>() ? Common::Ones<u64>(8) << (4 * 8) : 0;
            result |= imm8.Bit<5>() ? Common::Ones<u64>(8) << (5 * 8) : 0;
            result |= imm8.Bit<6>() ? Common::Ones<u64>(8) << (6 * 8) : 0;
            result |= imm8.Bit<7>() ? Common::Ones<u64>(8) << (7 * 8) : 0;
            return result;
        }
        if (cmode.Bit<0>() && !op) {
            u64 result = 0;
            result |= imm8.Bit<7>() ? 0x80000000 : 0;
            result |= imm8.Bit<6>() ? 0x3E000000 : 0x40000000;
            result |= imm8.Bits<0, 5, u64>() << 19;
            return Common::Replicate<u64>(result, 32);
        }
        if (cmode.Bit<0>() && op) {
            u64 result = 0;
            result |= imm8.Bit<7>() ? 0x80000000'00000000 : 0;
            result |= imm8.Bit<6>() ? 0x3FC00000'00000000 : 0x40000000'00000000;
            result |= imm8.Bits<0, 5, u64>() << 48;
            return result;
        }
    }
    UNREACHABLE();
    return 0;
}

IR::UAny TranslatorVisitor::I(size_t bitsize, u64 value) {
    switch (bitsize) {
    case 8:
        return ir.Imm8(static_cast<u8>(value));
    case 16:
        return ir.Imm16(static_cast<u16>(value));
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
        ASSERT_MSG(false, "SP - set : Invalid bitsize");
    }
}

IR::U128 TranslatorVisitor::V(size_t bitsize, Vec vec) {
    switch (bitsize) {
    case 32:
        return ir.GetS(vec);
    case 64:
        return ir.GetD(vec);
    case 128:
        return ir.GetQ(vec);
    default:
        ASSERT_MSG(false, "V - get : Invalid bitsize");
    }
}

void TranslatorVisitor::V(size_t bitsize, Vec vec, IR::U128 value) {
    switch (bitsize) {
    case 32:
        ir.SetS(vec, value);
        return;
    case 64:
        // TODO: Remove VectorZeroUpper when possible.
        ir.SetD(vec, ir.VectorZeroUpper(value));
        return;
    case 128:
        ir.SetQ(vec, value);
        return;
    default:
        ASSERT_MSG(false, "V - Set : Invalid bitsize");
    }
}

IR::UAnyU128 TranslatorVisitor::V_scalar(size_t bitsize, Vec vec) {
    if (bitsize == 128) {
        return V(128, vec);
    }
    // TODO: Optimize
    return ir.VectorGetElement(bitsize, ir.GetQ(vec), 0);
}

void TranslatorVisitor::V_scalar(size_t bitsize, Vec vec, IR::UAnyU128 value) {
    if (bitsize == 128) {
        return V(128, vec, value);
    }
    // TODO: Optimize
    ir.SetQ(vec, ir.ZeroExtendToQuad(value));
}

IR::U128 TranslatorVisitor::Vpart(size_t bitsize, Vec vec, size_t part) {
    ASSERT(part == 0 || part == 1);
    ASSERT(bitsize == 64);
    if (part == 0) {
        return V(64, vec);
    }
    return ir.ZeroExtendToQuad(ir.VectorGetElement(bitsize, V(128, vec), part));
}

void TranslatorVisitor::Vpart(size_t bitsize, Vec vec, size_t part, IR::U128 value) {
    ASSERT(part == 0 || part == 1);
    if (part == 0) {
        ASSERT(bitsize == 64);
        V(128, vec, ir.VectorZeroExtend(bitsize, value));
    } else {
        ASSERT(bitsize == 64);
        V(128, vec, ir.VectorInterleaveLower(64, V(128, vec), value));
    }
}

IR::UAny TranslatorVisitor::Vpart_scalar(size_t bitsize, Vec vec, size_t part) {
    ASSERT(part == 0 || part == 1);
    if (part == 0) {
        ASSERT(bitsize == 8 || bitsize == 16 || bitsize == 32 || bitsize == 64);
    } else {
        ASSERT(bitsize == 64);
    }
    return ir.VectorGetElement(bitsize, V(128, vec), part);
}

void TranslatorVisitor::Vpart_scalar(size_t bitsize, Vec vec, size_t part, IR::UAny value) {
    ASSERT(part == 0 || part == 1);
    if (part == 0) {
        ASSERT(bitsize == 8 || bitsize == 16 || bitsize == 32 || bitsize == 64);
        V(128, vec, ir.ZeroExtendToQuad(value));
    } else {
        ASSERT(bitsize == 64);
        V(128, vec, ir.VectorSetElement(64, V(128, vec), 1, value));
    }
}

IR::UAnyU128 TranslatorVisitor::Mem(IR::U64 address, size_t bytesize, AccType /*acctype*/) {
    switch (bytesize) {
    case 1:
        return ir.ReadMemory8(address);
    case 2:
        return ir.ReadMemory16(address);
    case 4:
        return ir.ReadMemory32(address);
    case 8:
        return ir.ReadMemory64(address);
    case 16:
        return ir.ReadMemory128(address);
    default:
        ASSERT_MSG(false, "Invalid bytesize parameter {}", bytesize);
        return {};
    }
}

void TranslatorVisitor::Mem(IR::U64 address, size_t bytesize, AccType /*acctype*/, IR::UAnyU128 value) {
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
    case 16:
        ir.WriteMemory128(address, value);
        return;
    default:
        ASSERT_MSG(false, "Invalid bytesize parameter {}", bytesize);
        return;
    }
}

IR::U32 TranslatorVisitor::ExclusiveMem(IR::U64 address, size_t bytesize, AccType /*acctype*/, IR::UAnyU128 value) {
    switch (bytesize) {
    case 1:
        return ir.ExclusiveWriteMemory8(address, value);
    case 2:
        return ir.ExclusiveWriteMemory16(address, value);
    case 4:
        return ir.ExclusiveWriteMemory32(address, value);
    case 8:
        return ir.ExclusiveWriteMemory64(address, value);
    case 16:
        return ir.ExclusiveWriteMemory128(address, value);
    default:
        ASSERT_MSG(false, "Invalid bytesize parameter {}", bytesize);
        return {};
    }
}

IR::U32U64 TranslatorVisitor::SignExtend(IR::UAny value, size_t to_size) {
    switch (to_size) {
    case 32:
        return ir.SignExtendToWord(value);
    case 64:
        return ir.SignExtendToLong(value);
    default:
        ASSERT_MSG(false, "Invalid size parameter {}", to_size);
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
        ASSERT_MSG(false, "Invalid size parameter {}", to_size);
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

} // namespace Dynarmic::A64
