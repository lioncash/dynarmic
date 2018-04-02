/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "frontend/ir/ir_emitter.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic::IR {

U1 IREmitter::Imm1(bool imm1) const {
    return U1(Value(imm1));
}

U8 IREmitter::Imm8(u8 imm8) const {
    return U8(Value(imm8));
}

U16 IREmitter::Imm16(u16 imm16) const {
    return U16(Value(imm16));
}

U32 IREmitter::Imm32(u32 imm32) const {
    return U32(Value(imm32));
}

U64 IREmitter::Imm64(u64 imm64) const {
    return U64(Value(imm64));
}

void IREmitter::PushRSB(const LocationDescriptor& return_location) {
    Inst(Opcode::PushRSB, IR::Value(return_location.Value()));
}

U64 IREmitter::Pack2x32To1x64(const U32& lo, const U32& hi) {
    return Inst<U64>(Opcode::Pack2x32To1x64, lo, hi);
}

U128 IREmitter::Pack2x64To1x128(const U64& lo, const U64& hi) {
    return Inst<U128>(Opcode::Pack2x64To1x128, lo, hi);
}

U32 IREmitter::LeastSignificantWord(const U64& value) {
    return Inst<U32>(Opcode::LeastSignificantWord, value);
}

ResultAndCarry<U32> IREmitter::MostSignificantWord(const U64& value) {
    auto result = Inst<U32>(Opcode::MostSignificantWord, value);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    return {result, carry_out};
}

U16 IREmitter::LeastSignificantHalf(U32U64 value) {
    if (value.GetType() == Type::U64) {
        value = LeastSignificantWord(value);
    }
    return Inst<U16>(Opcode::LeastSignificantHalf, value);
}

U8 IREmitter::LeastSignificantByte(U32U64 value) {
    if (value.GetType() == Type::U64) {
        value = LeastSignificantWord(value);
    }
    return Inst<U8>(Opcode::LeastSignificantByte, value);
}

U1 IREmitter::MostSignificantBit(const U32& value) {
    return Inst<U1>(Opcode::MostSignificantBit, value);
}

U1 IREmitter::IsZero(const U32& value) {
    return Inst<U1>(Opcode::IsZero32, value);
}

U1 IREmitter::IsZero(const U64& value) {
    return Inst<U1>(Opcode::IsZero64, value);
}

U1 IREmitter::IsZero(const U32U64& value) {
    if (value.GetType() == Type::U32) {
        return Inst<U1>(Opcode::IsZero32, value);
    } else {
        return Inst<U1>(Opcode::IsZero64, value);
    }
}

U1 IREmitter::TestBit(const U32U64& value, const U8& bit) {
    if (value.GetType() == Type::U32) {
        return Inst<U1>(Opcode::TestBit, IndeterminateExtendToLong(value), bit);
    } else {
        return Inst<U1>(Opcode::TestBit, value, bit);
    }
}

U32 IREmitter::ConditionalSelect(Cond cond, const U32& a, const U32& b) {
    return Inst<U32>(Opcode::ConditionalSelect32, Value{cond}, a, b);
}

U64 IREmitter::ConditionalSelect(Cond cond, const U64& a, const U64& b) {
    return Inst<U64>(Opcode::ConditionalSelect64, Value{cond}, a, b);
}

NZCV IREmitter::ConditionalSelect(Cond cond, const NZCV& a, const NZCV& b) {
    return Inst<NZCV>(Opcode::ConditionalSelectNZCV, Value{cond}, a, b);
}

U32U64 IREmitter::ConditionalSelect(Cond cond, const U32U64& a, const U32U64& b) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::ConditionalSelect32, Value{cond}, a, b);
    } else {
        return Inst<U64>(Opcode::ConditionalSelect64, Value{cond}, a, b);
    }
}

NZCV IREmitter::NZCVFromPackedFlags(const U32& a) {
    return Inst<NZCV>(Opcode::NZCVFromPackedFlags, a);
}

NZCV IREmitter::NZCVFrom(const Value& value) {
    return Inst<NZCV>(Opcode::GetNZCVFromOp, value);
}

ResultAndCarry<U32> IREmitter::LogicalShiftLeft(const U32& value_in, const U8& shift_amount, const U1& carry_in) {
    auto result = Inst<U32>(Opcode::LogicalShiftLeft32, value_in, shift_amount, carry_in);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    return {result, carry_out};
}

ResultAndCarry<U32> IREmitter::LogicalShiftRight(const U32& value_in, const U8& shift_amount, const U1& carry_in) {
    auto result = Inst<U32>(Opcode::LogicalShiftRight32, value_in, shift_amount, carry_in);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    return {result, carry_out};
}

ResultAndCarry<U32> IREmitter::ArithmeticShiftRight(const U32& value_in, const U8& shift_amount, const U1& carry_in) {
    auto result = Inst<U32>(Opcode::ArithmeticShiftRight32, value_in, shift_amount, carry_in);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    return {result, carry_out};
}

ResultAndCarry<U32> IREmitter::RotateRight(const U32& value_in, const U8& shift_amount, const U1& carry_in) {
    auto result = Inst<U32>(Opcode::RotateRight32, value_in, shift_amount, carry_in);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    return {result, carry_out};
}

ResultAndCarry<U32> IREmitter::RotateRightExtended(const U32& value_in, const U1& carry_in) {
    auto result = Inst<U32>(Opcode::RotateRightExtended, value_in, carry_in);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    return {result, carry_out};
}

U32 IREmitter::LogicalShiftLeft(const U32& value_in, const U8& shift_amount) {
    return Inst<U32>(Opcode::LogicalShiftLeft32, value_in, shift_amount, Imm1(0));
}

U64 IREmitter::LogicalShiftLeft(const U64& value_in, const U8& shift_amount) {
    return Inst<U64>(Opcode::LogicalShiftLeft64, value_in, shift_amount);
}

U32 IREmitter::LogicalShiftRight(const U32& value_in, const U8& shift_amount) {
    return Inst<U32>(Opcode::LogicalShiftRight32, value_in, shift_amount, Imm1(0));
}

U64 IREmitter::LogicalShiftRight(const U64& value_in, const U8& shift_amount) {
    return Inst<U64>(Opcode::LogicalShiftRight64, value_in, shift_amount);
}

U32U64 IREmitter::LogicalShiftLeft(const U32U64& value_in, const U8& shift_amount) {
    if (value_in.GetType() == Type::U32) {
        return Inst<U32>(Opcode::LogicalShiftLeft32, value_in, shift_amount, Imm1(0));
    } else {
        return Inst<U64>(Opcode::LogicalShiftLeft64, value_in, shift_amount);
    }
}

U32U64 IREmitter::LogicalShiftRight(const U32U64& value_in, const U8& shift_amount) {
    if (value_in.GetType() == Type::U32) {
        return Inst<U32>(Opcode::LogicalShiftRight32, value_in, shift_amount, Imm1(0));
    } else {
        return Inst<U64>(Opcode::LogicalShiftRight64, value_in, shift_amount);
    }
}

U32U64 IREmitter::ArithmeticShiftRight(const U32U64& value_in, const U8& shift_amount) {
    if (value_in.GetType() == Type::U32) {
        return Inst<U32>(Opcode::ArithmeticShiftRight32, value_in, shift_amount, Imm1(0));
    } else {
        return Inst<U64>(Opcode::ArithmeticShiftRight64, value_in, shift_amount);
    }
}

U32U64 IREmitter::RotateRight(const U32U64& value_in, const U8& shift_amount) {
    if (value_in.GetType() == Type::U32) {
        return Inst<U32>(Opcode::RotateRight32, value_in, shift_amount, Imm1(0));
    } else {
        return Inst<U64>(Opcode::RotateRight64, value_in, shift_amount);
    }
}

ResultAndCarryAndOverflow<U32> IREmitter::AddWithCarry(const U32& a, const U32& b, const U1& carry_in) {
    auto result = Inst<U32>(Opcode::Add32, a, b, carry_in);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    auto overflow = Inst<U1>(Opcode::GetOverflowFromOp, result);
    return {result, carry_out, overflow};
}

U32U64 IREmitter::AddWithCarry(const U32U64& a, const U32U64& b, const U1& carry_in) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Add32, a, b, carry_in);
    } else {
        return Inst<U64>(Opcode::Add64, a, b, carry_in);
    }
}

U32 IREmitter::Add(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::Add32, a, b, Imm1(0));
}

U64 IREmitter::Add(const U64& a, const U64& b) {
    return Inst<U64>(Opcode::Add64, a, b, Imm1(0));
}

U32U64 IREmitter::Add(const U32U64& a, const U32U64& b) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Add32, a, b, Imm1(0));
    } else {
        return Inst<U64>(Opcode::Add64, a, b, Imm1(0));
    }
}

ResultAndCarryAndOverflow<U32> IREmitter::SubWithCarry(const U32& a, const U32& b, const U1& carry_in) {
    // This is equivalent to AddWithCarry(a, Not(b), carry_in).
    auto result = Inst<U32>(Opcode::Sub32, a, b, carry_in);
    auto carry_out = Inst<U1>(Opcode::GetCarryFromOp, result);
    auto overflow = Inst<U1>(Opcode::GetOverflowFromOp, result);
    return {result, carry_out, overflow};
}

U32U64 IREmitter::SubWithCarry(const U32U64& a, const U32U64& b, const U1& carry_in) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Sub32, a, b, carry_in);
    } else {
        return Inst<U64>(Opcode::Sub64, a, b, carry_in);
    }
}

U32 IREmitter::Sub(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::Sub32, a, b, Imm1(1));
}

U64 IREmitter::Sub(const U64& a, const U64& b) {
    return Inst<U64>(Opcode::Sub64, a, b, Imm1(1));
}

U32U64 IREmitter::Sub(const U32U64& a, const U32U64& b) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Sub32, a, b, Imm1(1));
    } else {
        return Inst<U64>(Opcode::Sub64, a, b, Imm1(1));
    }
}

U32 IREmitter::Mul(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::Mul32, a, b);
}

U64 IREmitter::Mul(const U64& a, const U64& b) {
    return Inst<U64>(Opcode::Mul64, a, b);
}

U32U64 IREmitter::Mul(const U32U64& a, const U32U64& b) {
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Mul32, a, b);
    }

    return Inst<U64>(Opcode::Mul64, a, b);
}

U64 IREmitter::UnsignedMultiplyHigh(const U64& a, const U64& b) {
    return Inst<U64>(Opcode::UnsignedMultiplyHigh64, a, b);
}

U64 IREmitter::SignedMultiplyHigh(const U64& a, const U64& b) {
    return Inst<U64>(Opcode::SignedMultiplyHigh64, a, b);
}

U32 IREmitter::UnsignedDiv(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::UnsignedDiv32, a, b);
}

U64 IREmitter::UnsignedDiv(const U64& a, const U64& b) {
    return Inst<U64>(Opcode::UnsignedDiv64, a, b);
}

U32U64 IREmitter::UnsignedDiv(const U32U64& a, const U32U64& b) {
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::UnsignedDiv32, a, b);
    }

    return Inst<U64>(Opcode::UnsignedDiv64, a, b);
}

U32 IREmitter::SignedDiv(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::SignedDiv32, a, b);
}

U64 IREmitter::SignedDiv(const U64& a, const U64& b) {
    return Inst<U64>(Opcode::SignedDiv64, a, b);
}

U32U64 IREmitter::SignedDiv(const U32U64& a, const U32U64& b) {
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::SignedDiv32, a, b);
    }

    return Inst<U64>(Opcode::SignedDiv64, a, b);
}

U32 IREmitter::And(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::And32, a, b);
}

U32U64 IREmitter::And(const U32U64& a, const U32U64& b) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::And32, a, b);
    } else {
        return Inst<U64>(Opcode::And64, a, b);
    }
}

U32 IREmitter::Eor(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::Eor32, a, b);
}

U32U64 IREmitter::Eor(const U32U64& a, const U32U64& b) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Eor32, a, b);
    } else {
        return Inst<U64>(Opcode::Eor64, a, b);
    }
}

U32 IREmitter::Or(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::Or32, a, b);
}

U32U64 IREmitter::Or(const U32U64& a, const U32U64& b) {
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Or32, a, b);
    } else {
        return Inst<U64>(Opcode::Or64, a, b);
    }
}

U32 IREmitter::Not(const U32& a) {
    return Inst<U32>(Opcode::Not32, a);
}

U32U64 IREmitter::Not(const U32U64& a) {
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::Not32, a);
    } else {
        return Inst<U64>(Opcode::Not64, a);
    }
}

U64 IREmitter::SignExtendToLong(const UAny& a) {
    switch (a.GetType()) {
    case Type::U8:
        return Inst<U64>(Opcode::SignExtendByteToLong, a);
    case Type::U16:
        return Inst<U64>(Opcode::SignExtendHalfToLong, a);
    case Type::U32:
        return Inst<U64>(Opcode::SignExtendWordToLong, a);
    case Type::U64:
        return U64(a);
    default:
        ASSERT_MSG(false, "Unreachable");
        return {};
    }
}

U32 IREmitter::SignExtendToWord(const UAny& a) {
    switch (a.GetType()) {
    case Type::U8:
        return Inst<U32>(Opcode::SignExtendByteToWord, a);
    case Type::U16:
        return Inst<U32>(Opcode::SignExtendHalfToWord, a);
    case Type::U32:
        return U32(a);
    case Type::U64:
        return Inst<U32>(Opcode::LeastSignificantWord, a);
    default:
        ASSERT_MSG(false, "Unreachable");
        return {};
    }
}

U64 IREmitter::SignExtendWordToLong(const U32& a) {
    return Inst<U64>(Opcode::SignExtendWordToLong, a);
}

U32 IREmitter::SignExtendHalfToWord(const U16& a) {
    return Inst<U32>(Opcode::SignExtendHalfToWord, a);
}

U32 IREmitter::SignExtendByteToWord(const U8& a) {
    return Inst<U32>(Opcode::SignExtendByteToWord, a);
}

U64 IREmitter::ZeroExtendToLong(const UAny& a) {
    switch (a.GetType()) {
    case Type::U8:
        return Inst<U64>(Opcode::ZeroExtendByteToLong, a);
    case Type::U16:
        return Inst<U64>(Opcode::ZeroExtendHalfToLong, a);
    case Type::U32:
        return Inst<U64>(Opcode::ZeroExtendWordToLong, a);
    case Type::U64:
        return U64(a);
    default:
        ASSERT_MSG(false, "Unreachable");
        return {};
    }
}

U32 IREmitter::ZeroExtendToWord(const UAny& a) {
    switch (a.GetType()) {
    case Type::U8:
        return Inst<U32>(Opcode::ZeroExtendByteToWord, a);
    case Type::U16:
        return Inst<U32>(Opcode::ZeroExtendHalfToWord, a);
    case Type::U32:
        return U32(a);
    case Type::U64:
        return Inst<U32>(Opcode::LeastSignificantWord, a);
    default:
        ASSERT_MSG(false, "Unreachable");
        return {};
    }
}

U128 IREmitter::ZeroExtendToQuad(const UAny& a) {
    return Inst<U128>(Opcode::ZeroExtendLongToQuad, ZeroExtendToLong(a));
}

U64 IREmitter::ZeroExtendWordToLong(const U32& a) {
    return Inst<U64>(Opcode::ZeroExtendWordToLong, a);
}

U32 IREmitter::ZeroExtendHalfToWord(const U16& a) {
    return Inst<U32>(Opcode::ZeroExtendHalfToWord, a);
}

U32 IREmitter::ZeroExtendByteToWord(const U8& a) {
    return Inst<U32>(Opcode::ZeroExtendByteToWord, a);
}

U32 IREmitter::IndeterminateExtendToWord(const UAny& a) {
    // TODO: Implement properly
    return ZeroExtendToWord(a);
}

U64 IREmitter::IndeterminateExtendToLong(const UAny& a) {
    // TODO: Implement properly
    return ZeroExtendToLong(a);
}

U32 IREmitter::ByteReverseWord(const U32& a) {
    return Inst<U32>(Opcode::ByteReverseWord, a);
}

U16 IREmitter::ByteReverseHalf(const U16& a) {
    return Inst<U16>(Opcode::ByteReverseHalf, a);
}

U64 IREmitter::ByteReverseDual(const U64& a) {
    return Inst<U64>(Opcode::ByteReverseDual, a);
}

U32 IREmitter::CountLeadingZeros(const U32& a) {
    return Inst<U32>(Opcode::CountLeadingZeros32, a);
}

U64 IREmitter::CountLeadingZeros(const U64& a) {
    return Inst<U64>(Opcode::CountLeadingZeros64, a);
}

U32U64 IREmitter::CountLeadingZeros(const U32U64& a) {
    if (a.GetType() == IR::Type::U32) {
        return Inst<U32>(Opcode::CountLeadingZeros32, a);
    }

    return Inst<U64>(Opcode::CountLeadingZeros64, a);
}

U32 IREmitter::ExtractRegister(const U32& a, const U32& b, const U8& lsb) {
    return Inst<U32>(Opcode::ExtractRegister32, a, b, lsb);
}

U64 IREmitter::ExtractRegister(const U64& a, const U64& b, const U8& lsb) {
    return Inst<U64>(Opcode::ExtractRegister64, a, b, lsb);
}

U32U64 IREmitter::ExtractRegister(const U32U64& a, const U32U64& b, const U8& lsb) {
    if (a.GetType() == IR::Type::U32) {
        return Inst<U32>(Opcode::ExtractRegister32, a, b, lsb);
    }

    return Inst<U64>(Opcode::ExtractRegister64, a, b, lsb);
}

ResultAndOverflow<U32> IREmitter::SignedSaturatedAdd(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::SignedSaturatedAdd, a, b);
    auto overflow = Inst<U1>(Opcode::GetOverflowFromOp, result);
    return {result, overflow};
}

ResultAndOverflow<U32> IREmitter::SignedSaturatedSub(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::SignedSaturatedSub, a, b);
    auto overflow = Inst<U1>(Opcode::GetOverflowFromOp, result);
    return {result, overflow};
}

ResultAndOverflow<U32> IREmitter::UnsignedSaturation(const U32& a, size_t bit_size_to_saturate_to) {
    ASSERT(bit_size_to_saturate_to <= 31);
    auto result = Inst<U32>(Opcode::UnsignedSaturation, a, Imm8(static_cast<u8>(bit_size_to_saturate_to)));
    auto overflow = Inst<U1>(Opcode::GetOverflowFromOp, result);
    return {result, overflow};
}

ResultAndOverflow<U32> IREmitter::SignedSaturation(const U32& a, size_t bit_size_to_saturate_to) {
    ASSERT(bit_size_to_saturate_to >= 1 && bit_size_to_saturate_to <= 32);
    auto result = Inst<U32>(Opcode::SignedSaturation, a, Imm8(static_cast<u8>(bit_size_to_saturate_to)));
    auto overflow = Inst<U1>(Opcode::GetOverflowFromOp, result);
    return {result, overflow};
}

ResultAndGE<U32> IREmitter::PackedAddU8(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedAddU8, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedAddS8(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedAddS8, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedAddU16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedAddU16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedAddS16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedAddS16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedSubU8(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedSubU8, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedSubS8(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedSubS8, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedSubU16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedSubU16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedSubS16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedSubS16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedAddSubU16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedAddSubU16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedAddSubS16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedAddSubS16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedSubAddU16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedSubAddU16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

ResultAndGE<U32> IREmitter::PackedSubAddS16(const U32& a, const U32& b) {
    auto result = Inst<U32>(Opcode::PackedSubAddS16, a, b);
    auto ge = Inst<U32>(Opcode::GetGEFromOp, result);
    return {result, ge};
}

U32 IREmitter::PackedHalvingAddU8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingAddU8, a, b);
}

U32 IREmitter::PackedHalvingAddS8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingAddS8, a, b);
}

U32 IREmitter::PackedHalvingSubU8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingSubU8, a, b);
}

U32 IREmitter::PackedHalvingSubS8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingSubS8, a, b);
}

U32 IREmitter::PackedHalvingAddU16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingAddU16, a, b);
}

U32 IREmitter::PackedHalvingAddS16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingAddS16, a, b);
}

U32 IREmitter::PackedHalvingSubU16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingSubU16, a, b);
}

U32 IREmitter::PackedHalvingSubS16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingSubS16, a, b);
}

U32 IREmitter::PackedHalvingAddSubU16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingAddSubU16, a, b);
}

U32 IREmitter::PackedHalvingAddSubS16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingAddSubS16, a, b);
}

U32 IREmitter::PackedHalvingSubAddU16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingSubAddU16, a, b);
}

U32 IREmitter::PackedHalvingSubAddS16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedHalvingSubAddS16, a, b);
}

U32 IREmitter::PackedSaturatedAddU8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedAddU8, a, b);
}

U32 IREmitter::PackedSaturatedAddS8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedAddS8, a, b);
}

U32 IREmitter::PackedSaturatedSubU8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedSubU8, a, b);
}

U32 IREmitter::PackedSaturatedSubS8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedSubS8, a, b);
}

U32 IREmitter::PackedSaturatedAddU16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedAddU16, a, b);
}

U32 IREmitter::PackedSaturatedAddS16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedAddS16, a, b);
}

U32 IREmitter::PackedSaturatedSubU16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedSubU16, a, b);
}

U32 IREmitter::PackedSaturatedSubS16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSaturatedSubS16, a, b);
}

U32 IREmitter::PackedAbsDiffSumS8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedAbsDiffSumS8, a, b);
}

U32 IREmitter::PackedSelect(const U32& ge, const U32& a, const U32& b) {
    return Inst<U32>(Opcode::PackedSelect, ge, a, b);
}

U32 IREmitter::CRC32Castagnoli8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::CRC32Castagnoli8, a, b);
}

U32 IREmitter::CRC32Castagnoli16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::CRC32Castagnoli16, a, b);
}

U32 IREmitter::CRC32Castagnoli32(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::CRC32Castagnoli32, a, b);
}

U32 IREmitter::CRC32Castagnoli64(const U32& a, const U64& b) {
    return Inst<U32>(Opcode::CRC32Castagnoli64, a, b);
}

U32 IREmitter::CRC32ISO8(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::CRC32ISO8, a, b);
}

U32 IREmitter::CRC32ISO16(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::CRC32ISO16, a, b);
}

U32 IREmitter::CRC32ISO32(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::CRC32ISO32, a, b);
}

U32 IREmitter::CRC32ISO64(const U32& a, const U64& b) {
    return Inst<U32>(Opcode::CRC32ISO64, a, b);
}

U128 IREmitter::AESDecryptSingleRound(const U128& a) {
    return Inst<U128>(Opcode::AESDecryptSingleRound, a);
}

U128 IREmitter::AESEncryptSingleRound(const U128& a) {
    return Inst<U128>(Opcode::AESEncryptSingleRound, a);
}

U128 IREmitter::AESInverseMixColumns(const U128& a) {
    return Inst<U128>(Opcode::AESInverseMixColumns, a);
}

U128 IREmitter::AESMixColumns(const U128& a) {
    return Inst<U128>(Opcode::AESMixColumns, a);
}

UAny IREmitter::VectorGetElement(size_t esize, const U128& a, size_t index) {
    ASSERT_MSG(esize * index < 128, "Invalid index");
    switch (esize) {
    case 8:
        return Inst<U8>(Opcode::VectorGetElement8, a, Imm8(static_cast<u8>(index)));
    case 16:
        return Inst<U16>(Opcode::VectorGetElement16, a, Imm8(static_cast<u8>(index)));
    case 32:
        return Inst<U32>(Opcode::VectorGetElement32, a, Imm8(static_cast<u8>(index)));
    case 64:
        return Inst<U64>(Opcode::VectorGetElement64, a, Imm8(static_cast<u8>(index)));
    default:
        ASSERT_MSG(false, "Unreachable");
        return {};
    }
}

U128 IREmitter::VectorSetElement(size_t esize, const U128& a, size_t index, const IR::UAny& elem) {
    ASSERT_MSG(esize * index < 128, "Invalid index");
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorSetElement8, a, Imm8(static_cast<u8>(index)), elem);
    case 16:
        return Inst<U128>(Opcode::VectorSetElement16, a, Imm8(static_cast<u8>(index)), elem);
    case 32:
        return Inst<U128>(Opcode::VectorSetElement32, a, Imm8(static_cast<u8>(index)), elem);
    case 64:
        return Inst<U128>(Opcode::VectorSetElement64, a, Imm8(static_cast<u8>(index)), elem);
    default:
        ASSERT_MSG(false, "Unreachable");
        return {};
    }
}

U128 IREmitter::VectorAdd(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorAdd8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorAdd16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorAdd32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorAdd64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorAnd(const U128& a, const U128& b) {
    return Inst<U128>(Opcode::VectorAnd, a, b);
}

U128 IREmitter::VectorArithmeticShiftRight(size_t esize, const U128& a, u8 shift_amount) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorArithmeticShiftRight8, a, Imm8(shift_amount));
    case 16:
        return Inst<U128>(Opcode::VectorArithmeticShiftRight16, a, Imm8(shift_amount));
    case 32:
        return Inst<U128>(Opcode::VectorArithmeticShiftRight32, a, Imm8(shift_amount));
    case 64:
        return Inst<U128>(Opcode::VectorArithmeticShiftRight64, a, Imm8(shift_amount));
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorBroadcastLower(size_t esize, const UAny& a) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorBroadcastLower8, U8(a));
    case 16:
        return Inst<U128>(Opcode::VectorBroadcastLower16, U16(a));
    case 32:
        return Inst<U128>(Opcode::VectorBroadcastLower32, U32(a));
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorBroadcast(size_t esize, const UAny& a) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorBroadcast8, U8(a));
    case 16:
        return Inst<U128>(Opcode::VectorBroadcast16, U16(a));
    case 32:
        return Inst<U128>(Opcode::VectorBroadcast32, U32(a));
    case 64:
        return Inst<U128>(Opcode::VectorBroadcast64, U64(a));
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorEor(const U128& a, const U128& b) {
    return Inst<U128>(Opcode::VectorEor, a, b);
}

U128 IREmitter::VectorEqual(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorEqual8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorEqual16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorEqual32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorEqual64, a, b);
    case 128:
        return Inst<U128>(Opcode::VectorEqual128, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorExtract(const U128& a, const U128& b, size_t position) {
    ASSERT(position <= 128);
    return Inst<U128>(Opcode::VectorExtract, a, b, Imm8(static_cast<u8>(position)));
}

U128 IREmitter::VectorExtractLower(const U128& a, const U128& b, size_t position) {
    ASSERT(position <= 64);
    return Inst<U128>(Opcode::VectorExtractLower, a, b, Imm8(static_cast<u8>(position)));
}

U128 IREmitter::VectorGreaterSigned(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorGreaterS8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorGreaterS16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorGreaterS32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorGreaterS64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorGreaterEqualSigned(size_t esize, const U128& a, const U128& b) {
    return VectorOr(VectorGreaterSigned(esize, a, b), VectorEqual(esize, a, b));
}

U128 IREmitter::VectorGreaterEqualUnsigned(size_t esize, const U128& a, const U128& b) {
    return VectorEqual(esize, VectorMaxUnsigned(esize, a, b), a);
}

U128 IREmitter::VectorGreaterUnsigned(size_t esize, const U128& a, const U128& b) {
    return VectorNot(VectorEqual(esize, VectorMinUnsigned(esize, a, b), a));
}

U128 IREmitter::VectorInterleaveLower(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorInterleaveLower8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorInterleaveLower16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorInterleaveLower32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorInterleaveLower64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorInterleaveUpper(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorInterleaveUpper8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorInterleaveUpper16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorInterleaveUpper32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorInterleaveUpper64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorLessEqualSigned(size_t esize, const U128& a, const U128& b) {
    return VectorNot(VectorGreaterSigned(esize, a, b));
}

U128 IREmitter::VectorLessEqualUnsigned(size_t esize, const U128& a, const U128& b) {
    return VectorEqual(esize, VectorMinUnsigned(esize, a, b), a);
}

U128 IREmitter::VectorLessSigned(size_t esize, const U128& a, const U128& b) {
    return VectorNot(VectorOr(VectorGreaterSigned(esize, a, b), VectorEqual(esize, a, b)));
}

U128 IREmitter::VectorLessUnsigned(size_t esize, const U128& a, const U128& b) {
    return VectorNot(VectorEqual(esize, VectorMaxUnsigned(esize, a, b), a));
}

U128 IREmitter::VectorLogicalShiftLeft(size_t esize, const U128& a, u8 shift_amount) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorLogicalShiftLeft8, a, Imm8(shift_amount));
    case 16:
        return Inst<U128>(Opcode::VectorLogicalShiftLeft16, a, Imm8(shift_amount));
    case 32:
        return Inst<U128>(Opcode::VectorLogicalShiftLeft32, a, Imm8(shift_amount));
    case 64:
        return Inst<U128>(Opcode::VectorLogicalShiftLeft64, a, Imm8(shift_amount));
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorLogicalShiftRight(size_t esize, const U128& a, u8 shift_amount) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorLogicalShiftRight8, a, Imm8(shift_amount));
    case 16:
        return Inst<U128>(Opcode::VectorLogicalShiftRight16, a, Imm8(shift_amount));
    case 32:
        return Inst<U128>(Opcode::VectorLogicalShiftRight32, a, Imm8(shift_amount));
    case 64:
        return Inst<U128>(Opcode::VectorLogicalShiftRight64, a, Imm8(shift_amount));
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorLogicalVShift(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorLogicalVShift8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorLogicalVShift16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorLogicalVShift32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorLogicalVShift64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorMaxSigned(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorMaxS8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorMaxS16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorMaxS32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorMaxS64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorMaxUnsigned(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorMaxU8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorMaxU16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorMaxU32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorMaxU64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorMinSigned(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorMinS8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorMinS16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorMinS32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorMinS64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorMinUnsigned(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorMinU8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorMinU16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorMinU32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorMinU64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorMultiply(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorMultiply8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorMultiply16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorMultiply32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorMultiply64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorNarrow(size_t original_esize, const U128& a) {
    switch (original_esize) {
    case 16:
        return Inst<U128>(Opcode::VectorNarrow16, a);
    case 32:
        return Inst<U128>(Opcode::VectorNarrow32, a);
    case 64:
        return Inst<U128>(Opcode::VectorNarrow64, a);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorNot(const U128& a) {
    return Inst<U128>(Opcode::VectorNot, a);
}

U128 IREmitter::VectorOr(const U128& a, const U128& b) {
    return Inst<U128>(Opcode::VectorOr, a, b);
}

U128 IREmitter::VectorPairedAddLower(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorPairedAddLower8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorPairedAddLower16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorPairedAddLower32, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorPairedAdd(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorPairedAdd8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorPairedAdd16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorPairedAdd32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorPairedAdd64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorPopulationCount(const U128& a) {
    return Inst<U128>(Opcode::VectorPopulationCount, a);
}

U128 IREmitter::VectorShuffleHighHalfwords(const U128& a, u8 mask) {
    return Inst<U128>(Opcode::VectorShuffleHighHalfwords, a, mask);
}

U128 IREmitter::VectorShuffleLowHalfwords(const U128& a, u8 mask) {
    return Inst<U128>(Opcode::VectorShuffleLowHalfwords, a, mask);
}

U128 IREmitter::VectorShuffleWords(const U128& a, u8 mask) {
    return Inst<U128>(Opcode::VectorShuffleWords, a, mask);
}

U128 IREmitter::VectorSignExtend(size_t original_esize, const U128& a) {
    switch (original_esize) {
    case 8:
        return Inst<U128>(Opcode::VectorSignExtend8, a);
    case 16:
        return Inst<U128>(Opcode::VectorSignExtend16, a);
    case 32:
        return Inst<U128>(Opcode::VectorSignExtend32, a);
    case 64:
        return Inst<U128>(Opcode::VectorSignExtend64, a);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorSub(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorSub8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorSub16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorSub32, a, b);
    case 64:
        return Inst<U128>(Opcode::VectorSub64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorUnsignedAbsoluteDifference(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 8:
        return Inst<U128>(Opcode::VectorUnsignedAbsoluteDifference8, a, b);
    case 16:
        return Inst<U128>(Opcode::VectorUnsignedAbsoluteDifference16, a, b);
    case 32:
        return Inst<U128>(Opcode::VectorUnsignedAbsoluteDifference32, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorZeroExtend(size_t original_esize, const U128& a) {
    switch (original_esize) {
    case 8:
        return Inst<U128>(Opcode::VectorZeroExtend8, a);
    case 16:
        return Inst<U128>(Opcode::VectorZeroExtend16, a);
    case 32:
        return Inst<U128>(Opcode::VectorZeroExtend32, a);
    case 64:
        return Inst<U128>(Opcode::VectorZeroExtend64, a);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::VectorZeroUpper(const U128& a) {
    return Inst<U128>(Opcode::VectorZeroUpper, a);
}

U128 IREmitter::ZeroVector() {
    return Inst<U128>(Opcode::ZeroVector);
}

U32U64 IREmitter::FPAbs(const U32U64& a) {
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPAbs32, a);
    } else {
        return Inst<U64>(Opcode::FPAbs64, a);
    }
}

U32U64 IREmitter::FPAdd(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPAdd32, a, b);
    } else {
        return Inst<U64>(Opcode::FPAdd64, a, b);
    }
}

NZCV IREmitter::FPCompare(const U32U64& a, const U32U64& b, bool exc_on_qnan, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<NZCV>(Opcode::FPCompare32, a, b, Imm1(exc_on_qnan));
    } else {
        return Inst<NZCV>(Opcode::FPCompare64, a, b, Imm1(exc_on_qnan));
    }
}

U32U64 IREmitter::FPDiv(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPDiv32, a, b);
    } else {
        return Inst<U64>(Opcode::FPDiv64, a, b);
    }
}

U32U64 IREmitter::FPMax(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPMax32, a, b);
    } else {
        return Inst<U64>(Opcode::FPMax64, a, b);
    }
}

U32U64 IREmitter::FPMaxNumeric(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPMaxNumeric32, a, b);
    } else {
        return Inst<U64>(Opcode::FPMaxNumeric64, a, b);
    }
}

U32U64 IREmitter::FPMin(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPMin32, a, b);
    } else {
        return Inst<U64>(Opcode::FPMin64, a, b);
    }
}

U32U64 IREmitter::FPMinNumeric(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPMinNumeric32, a, b);
    } else {
        return Inst<U64>(Opcode::FPMinNumeric64, a, b);
    }
}

U32U64 IREmitter::FPMul(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPMul32, a, b);
    } else {
        return Inst<U64>(Opcode::FPMul64, a, b);
    }
}

U32U64 IREmitter::FPNeg(const U32U64& a) {
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPNeg32, a);
    } else {
        return Inst<U64>(Opcode::FPNeg64, a);
    }
}

U32U64 IREmitter::FPSqrt(const U32U64& a) {
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPSqrt32, a);
    } else {
        return Inst<U64>(Opcode::FPSqrt64, a);
    }
}

U32U64 IREmitter::FPSub(const U32U64& a, const U32U64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    ASSERT(a.GetType() == b.GetType());
    if (a.GetType() == Type::U32) {
        return Inst<U32>(Opcode::FPSub32, a, b);
    } else {
        return Inst<U64>(Opcode::FPSub64, a, b);
    }
}

U32 IREmitter::FPDoubleToSingle(const U64& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U32>(Opcode::FPDoubleToSingle, a);
}

U64 IREmitter::FPSingleToDouble(const U32& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U64>(Opcode::FPSingleToDouble, a);
}

U32 IREmitter::FPSingleToS32(const U32& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U32>(Opcode::FPSingleToS32, a, Imm1(round_towards_zero));
}

U32 IREmitter::FPSingleToU32(const U32& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U32>(Opcode::FPSingleToU32, a, Imm1(round_towards_zero));
}

U32 IREmitter::FPDoubleToS32(const U64& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U32>(Opcode::FPDoubleToS32, a, Imm1(round_towards_zero));
}

U32 IREmitter::FPDoubleToU32(const U64& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U32>(Opcode::FPDoubleToU32, a, Imm1(round_towards_zero));
}

U32 IREmitter::FPS32ToSingle(const U32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U32>(Opcode::FPS32ToSingle, a, Imm1(round_to_nearest));
}

U32 IREmitter::FPU32ToSingle(const U32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U32>(Opcode::FPU32ToSingle, a, Imm1(round_to_nearest));
}

U64 IREmitter::FPS32ToDouble(const U32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U64>(Opcode::FPS32ToDouble, a, Imm1(round_to_nearest));
}

U64 IREmitter::FPU32ToDouble(const U32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<U64>(Opcode::FPU32ToDouble, a, Imm1(round_to_nearest));
}

U128 IREmitter::FPVectorAdd(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 32:
        return Inst<U128>(Opcode::FPVectorAdd32, a, b);
    case 64:
        return Inst<U128>(Opcode::FPVectorAdd64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::FPVectorDiv(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 32:
        return Inst<U128>(Opcode::FPVectorDiv32, a, b);
    case 64:
        return Inst<U128>(Opcode::FPVectorDiv64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::FPVectorMul(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 32:
        return Inst<U128>(Opcode::FPVectorMul32, a, b);
    case 64:
        return Inst<U128>(Opcode::FPVectorMul64, a, b);
    }
    UNREACHABLE();
    return {};
}

U128 IREmitter::FPVectorSub(size_t esize, const U128& a, const U128& b) {
    switch (esize) {
    case 32:
        return Inst<U128>(Opcode::FPVectorSub32, a, b);
    case 64:
        return Inst<U128>(Opcode::FPVectorSub64, a, b);
    }
    UNREACHABLE();
    return {};
}

void IREmitter::Breakpoint() {
    Inst(Opcode::Breakpoint);
}

void IREmitter::SetTerm(const Terminal& terminal) {
    block.SetTerminal(terminal);
}

void IREmitter::SetInsertionPoint(IR::Inst* new_insertion_point) {
    insertion_point = IR::Block::iterator{*new_insertion_point};
}

void IREmitter::SetInsertionPoint(IR::Block::iterator new_insertion_point) {
    insertion_point = new_insertion_point;
}

} // namespace Dynarmic::IR
