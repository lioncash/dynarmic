/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "frontend/ir/ir_emitter.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic {
namespace IR {

void IREmitter::Unimplemented() {

}

U1 IREmitter::Imm1(bool imm1) {
    return U1(Value(imm1));
}

U8 IREmitter::Imm8(u8 imm8) {
    return U8(Value(imm8));
}

U32 IREmitter::Imm32(u32 imm32) {
    return U32(Value(imm32));
}

U64 IREmitter::Imm64(u64 imm64) {
    return U64(Value(imm64));
}

void IREmitter::PushRSB(const LocationDescriptor& return_location) {
    Inst(Opcode::PushRSB, IR::Value(return_location.Value()));
}

U64 IREmitter::Pack2x32To1x64(const U32& lo, const U32& hi) {
    return Inst<U64>(Opcode::Pack2x32To1x64, lo, hi);
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
    return Inst<U32>(Opcode::CountLeadingZeros, a);
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

F32 IREmitter::TransferToFP32(const U32& a) {
    return Inst<F32>(Opcode::TransferToFP32, a);
}

F64 IREmitter::TransferToFP64(const U64& a) {
    return Inst<F64>(Opcode::TransferToFP64, a);
}

U32 IREmitter::TransferFromFP32(const F32& a) {
    return Inst<U32>(Opcode::TransferFromFP32, a);
}

U64 IREmitter::TransferFromFP64(const F64& a) {
    return Inst<U64>(Opcode::TransferFromFP64, a);
}

F32 IREmitter::FPAbs32(const F32& a) {
    return Inst<F32>(Opcode::FPAbs32, a);
}

F64 IREmitter::FPAbs64(const F64& a) {
    return Inst<F64>(Opcode::FPAbs64, a);
}

F32 IREmitter::FPAdd32(const F32& a, const F32& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPAdd32, a, b);
}

F64 IREmitter::FPAdd64(const F64& a, const F64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F64>(Opcode::FPAdd64, a, b);
}

void IREmitter::FPCompare32(const F32& a, const F32& b, bool exc_on_qnan, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    Inst(Opcode::FPCompare32, a, b, Imm1(exc_on_qnan));
}

void IREmitter::FPCompare64(const F64& a, const F64& b, bool exc_on_qnan, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    Inst(Opcode::FPCompare64, a, b, Imm1(exc_on_qnan));
}

F32 IREmitter::FPDiv32(const F32& a, const F32& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPDiv32, a, b);
}

F64 IREmitter::FPDiv64(const F64& a, const F64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F64>(Opcode::FPDiv64, a, b);
}

F32 IREmitter::FPMul32(const F32& a, const F32& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPMul32, a, b);
}

F64 IREmitter::FPMul64(const F64& a, const F64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F64>(Opcode::FPMul64, a, b);
}

F32 IREmitter::FPNeg32(const F32& a) {
    return Inst<F32>(Opcode::FPNeg32, a);
}

F64 IREmitter::FPNeg64(const F64& a) {
    return Inst<F64>(Opcode::FPNeg64, a);
}

F32 IREmitter::FPSqrt32(const F32& a) {
    return Inst<F32>(Opcode::FPSqrt32, a);
}

F64 IREmitter::FPSqrt64(const F64& a) {
    return Inst<F64>(Opcode::FPSqrt64, a);
}

F32 IREmitter::FPSub32(const F32& a, const F32& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPSub32, a, b);
}

F64 IREmitter::FPSub64(const F64& a, const F64& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F64>(Opcode::FPSub64, a, b);
}

F32 IREmitter::FPDoubleToSingle(const F64& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPDoubleToSingle, a);
}

F64 IREmitter::FPSingleToDouble(const F32& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F64>(Opcode::FPSingleToDouble, a);
}

F32 IREmitter::FPSingleToS32(const F32& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPSingleToS32, a, Imm1(round_towards_zero));
}

F32 IREmitter::FPSingleToU32(const F32& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPSingleToU32, a, Imm1(round_towards_zero));
}

F32 IREmitter::FPDoubleToS32(const F32& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPDoubleToS32, a, Imm1(round_towards_zero));
}

F32 IREmitter::FPDoubleToU32(const F32& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPDoubleToU32, a, Imm1(round_towards_zero));
}

F32 IREmitter::FPS32ToSingle(const F32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPS32ToSingle, a, Imm1(round_to_nearest));
}

F32 IREmitter::FPU32ToSingle(const F32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F32>(Opcode::FPU32ToSingle, a, Imm1(round_to_nearest));
}

F64 IREmitter::FPS32ToDouble(const F32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F64>(Opcode::FPS32ToDouble, a, Imm1(round_to_nearest));
}

F64 IREmitter::FPU32ToDouble(const F32& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst<F64>(Opcode::FPU32ToDouble, a, Imm1(round_to_nearest));
}

void IREmitter::Breakpoint() {
    Inst(Opcode::Breakpoint);
}

void IREmitter::SetTerm(const Terminal& terminal) {
    block.SetTerminal(terminal);
}

} // namespace IR
} // namespace Dynarmic
