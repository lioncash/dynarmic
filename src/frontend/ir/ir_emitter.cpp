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

Value IREmitter::Imm1(bool imm1) {
    return Value(imm1);
}

Value IREmitter::Imm8(u8 imm8) {
    return Value(imm8);
}

Value IREmitter::Imm32(u32 imm32) {
    return Value(imm32);
}

Value IREmitter::Imm64(u64 imm64) {
    return Value(imm64);
}

Value IREmitter::Pack2x32To1x64(const Value& lo, const Value& hi) {
    return Inst(Opcode::Pack2x32To1x64, {lo, hi});
}

Value IREmitter::LeastSignificantWord(const Value& value) {
    return Inst(Opcode::LeastSignificantWord, {value});
}

IREmitter::ResultAndCarry IREmitter::MostSignificantWord(const Value& value) {
    auto result = Inst(Opcode::MostSignificantWord, {value});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

Value IREmitter::LeastSignificantHalf(const Value& value) {
    return Inst(Opcode::LeastSignificantHalf, {value});
}

Value IREmitter::LeastSignificantByte(const Value& value) {
    return Inst(Opcode::LeastSignificantByte, {value});
}

Value IREmitter::MostSignificantBit(const Value& value) {
    return Inst(Opcode::MostSignificantBit, {value});
}

Value IREmitter::IsZero(const Value& value) {
    return Inst(Opcode::IsZero, {value});
}

Value IREmitter::IsZero64(const Value& value) {
    return Inst(Opcode::IsZero64, {value});
}

IREmitter::ResultAndCarry IREmitter::LogicalShiftLeft(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::LogicalShiftLeft, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

IREmitter::ResultAndCarry IREmitter::LogicalShiftRight(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::LogicalShiftRight, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

Value IREmitter::LogicalShiftRight64(const Value& value_in, const Value& shift_amount) {
    return Inst(Opcode::LogicalShiftRight64, {value_in, shift_amount});
}

IREmitter::ResultAndCarry IREmitter::ArithmeticShiftRight(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::ArithmeticShiftRight, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

IREmitter::ResultAndCarry IREmitter::RotateRight(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::RotateRight, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

IREmitter::ResultAndCarry IREmitter::RotateRightExtended(const Value& value_in, const Value& carry_in) {
    auto result = Inst(Opcode::RotateRightExtended, {value_in, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

IREmitter::ResultAndCarryAndOverflow IREmitter::AddWithCarry(const Value& a, const Value& b, const Value& carry_in) {
    auto result = Inst(Opcode::AddWithCarry, {a, b, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, carry_out, overflow};
}

Value IREmitter::Add(const Value& a, const Value& b) {
    return Inst(Opcode::AddWithCarry, {a, b, Imm1(0)});
}

Value IREmitter::Add64(const Value& a, const Value& b) {
    return Inst(Opcode::Add64, {a, b});
}

IREmitter::ResultAndCarryAndOverflow IREmitter::SubWithCarry(const Value& a, const Value& b, const Value& carry_in) {
    // This is equivalent to AddWithCarry(a, Not(b), carry_in).
    auto result = Inst(Opcode::SubWithCarry, {a, b, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, carry_out, overflow};
}

Value IREmitter::Sub(const Value& a, const Value& b) {
    return Inst(Opcode::SubWithCarry, {a, b, Imm1(1)});
}

Value IREmitter::Sub64(const Value& a, const Value& b) {
    return Inst(Opcode::Sub64, {a, b});
}

Value IREmitter::Mul(const Value& a, const Value& b) {
    return Inst(Opcode::Mul, {a, b});
}

Value IREmitter::Mul64(const Value& a, const Value& b) {
    return Inst(Opcode::Mul64, {a, b});
}

Value IREmitter::And(const Value& a, const Value& b) {
    return Inst(Opcode::And, {a, b});
}

Value IREmitter::Eor(const Value& a, const Value& b) {
    return Inst(Opcode::Eor, {a, b});
}

Value IREmitter::Or(const Value& a, const Value& b) {
    return Inst(Opcode::Or, {a, b});
}

Value IREmitter::Not(const Value& a) {
    return Inst(Opcode::Not, {a});
}

Value IREmitter::SignExtendWordToLong(const Value& a) {
    return Inst(Opcode::SignExtendWordToLong, {a});
}

Value IREmitter::SignExtendHalfToWord(const Value& a) {
    return Inst(Opcode::SignExtendHalfToWord, {a});
}

Value IREmitter::SignExtendByteToWord(const Value& a) {
    return Inst(Opcode::SignExtendByteToWord, {a});
}

Value IREmitter::ZeroExtendWordToLong(const Value& a) {
    return Inst(Opcode::ZeroExtendWordToLong, {a});
}

Value IREmitter::ZeroExtendHalfToWord(const Value& a) {
    return Inst(Opcode::ZeroExtendHalfToWord, {a});
}

Value IREmitter::ZeroExtendByteToWord(const Value& a) {
    return Inst(Opcode::ZeroExtendByteToWord, {a});
}

Value IREmitter::ByteReverseWord(const Value& a) {
    return Inst(Opcode::ByteReverseWord, {a});
}

Value IREmitter::ByteReverseHalf(const Value& a) {
    return Inst(Opcode::ByteReverseHalf, {a});
}

Value IREmitter::ByteReverseDual(const Value& a) {
    return Inst(Opcode::ByteReverseDual, {a});
}

Value IREmitter::CountLeadingZeros(const Value& a) {
    return Inst(Opcode::CountLeadingZeros, {a});
}

IREmitter::ResultAndOverflow IREmitter::SignedSaturatedAdd(const Value& a, const Value& b) {
    auto result = Inst(Opcode::SignedSaturatedAdd, {a, b});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

IREmitter::ResultAndOverflow IREmitter::SignedSaturatedSub(const Value& a, const Value& b) {
    auto result = Inst(Opcode::SignedSaturatedSub, {a, b});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

IREmitter::ResultAndOverflow IREmitter::UnsignedSaturation(const Value& a, size_t bit_size_to_saturate_to) {
    ASSERT(bit_size_to_saturate_to <= 31);
    auto result = Inst(Opcode::UnsignedSaturation, {a, Imm8(static_cast<u8>(bit_size_to_saturate_to))});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

IREmitter::ResultAndOverflow IREmitter::SignedSaturation(const Value& a, size_t bit_size_to_saturate_to) {
    ASSERT(bit_size_to_saturate_to >= 1 && bit_size_to_saturate_to <= 32);
    auto result = Inst(Opcode::SignedSaturation, {a, Imm8(static_cast<u8>(bit_size_to_saturate_to))});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

IREmitter::ResultAndGE IREmitter::PackedAddU8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddU8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedAddS8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddS8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedAddU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedAddS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedSubU8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubU8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedSubS8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubS8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedSubU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedSubS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedAddSubU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddSubU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedAddSubS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddSubS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedSubAddU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubAddU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

IREmitter::ResultAndGE IREmitter::PackedSubAddS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubAddS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

Value IREmitter::PackedHalvingAddU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddU8, {a, b});
}

Value IREmitter::PackedHalvingAddS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddS8, {a, b});
}

Value IREmitter::PackedHalvingSubU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubU8, {a, b});
}

Value IREmitter::PackedHalvingSubS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubS8, {a, b});
}

Value IREmitter::PackedHalvingAddU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddU16, {a, b});
}

Value IREmitter::PackedHalvingAddS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddS16, {a, b});
}

Value IREmitter::PackedHalvingSubU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubU16, {a, b});
}

Value IREmitter::PackedHalvingSubS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubS16, {a, b});
}

Value IREmitter::PackedHalvingAddSubU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddSubU16, {a, b});
}

Value IREmitter::PackedHalvingAddSubS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddSubS16, {a, b});
}

Value IREmitter::PackedHalvingSubAddU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubAddU16, {a, b});
}

Value IREmitter::PackedHalvingSubAddS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubAddS16, {a, b});
}

Value IREmitter::PackedSaturatedAddU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddU8, {a, b});
}

Value IREmitter::PackedSaturatedAddS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddS8, {a, b});
}

Value IREmitter::PackedSaturatedSubU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubU8, {a, b});
}

Value IREmitter::PackedSaturatedSubS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubS8, {a, b});
}

Value IREmitter::PackedSaturatedAddU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddU16, {a, b});
}

Value IREmitter::PackedSaturatedAddS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddS16, {a, b});
}

Value IREmitter::PackedSaturatedSubU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubU16, {a, b});
}

Value IREmitter::PackedSaturatedSubS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubS16, {a, b});
}

Value IREmitter::PackedAbsDiffSumS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedAbsDiffSumS8, {a, b});
}

Value IREmitter::PackedSelect(const Value& ge, const Value& a, const Value& b) {
    return Inst(Opcode::PackedSelect, {ge, a, b});
}

Value IREmitter::TransferToFP32(const Value& a) {
    return Inst(Opcode::TransferToFP32, {a});
}

Value IREmitter::TransferToFP64(const Value& a) {
    return Inst(Opcode::TransferToFP64, {a});
}

Value IREmitter::TransferFromFP32(const Value& a) {
    return Inst(Opcode::TransferFromFP32, {a});
}

Value IREmitter::TransferFromFP64(const Value& a) {
    return Inst(Opcode::TransferFromFP64, {a});
}

Value IREmitter::FPAbs32(const Value& a) {
    return Inst(Opcode::FPAbs32, {a});
}

Value IREmitter::FPAbs64(const Value& a) {
    return Inst(Opcode::FPAbs64, {a});
}

Value IREmitter::FPAdd32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPAdd32, {a, b});
}

Value IREmitter::FPAdd64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPAdd64, {a, b});
}

void IREmitter::FPCompare32(const Value& a, const Value& b, bool exc_on_qnan, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    Inst(Opcode::FPCompare32, {a, b, Imm1(exc_on_qnan)});
}

void IREmitter::FPCompare64(const Value& a, const Value& b, bool exc_on_qnan, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    Inst(Opcode::FPCompare64, {a, b, Imm1(exc_on_qnan)});
}

Value IREmitter::FPDiv32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDiv32, {a, b});
}

Value IREmitter::FPDiv64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDiv64, {a, b});
}

Value IREmitter::FPMul32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPMul32, {a, b});
}

Value IREmitter::FPMul64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPMul64, {a, b});
}

Value IREmitter::FPNeg32(const Value& a) {
    return Inst(Opcode::FPNeg32, {a});
}

Value IREmitter::FPNeg64(const Value& a) {
    return Inst(Opcode::FPNeg64, {a});
}

Value IREmitter::FPSqrt32(const Value& a) {
    return Inst(Opcode::FPSqrt32, {a});
}

Value IREmitter::FPSqrt64(const Value& a) {
    return Inst(Opcode::FPSqrt64, {a});
}

Value IREmitter::FPSub32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSub32, {a, b});
}

Value IREmitter::FPSub64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSub64, {a, b});
}

Value IREmitter::FPDoubleToSingle(const Value& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDoubleToSingle, {a});
}

Value IREmitter::FPSingleToDouble(const Value& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSingleToDouble, {a});
}

Value IREmitter::FPSingleToS32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSingleToS32, {a, Imm1(round_towards_zero)});
}

Value IREmitter::FPSingleToU32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSingleToU32, {a, Imm1(round_towards_zero)});
}

Value IREmitter::FPDoubleToS32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDoubleToS32, {a, Imm1(round_towards_zero)});
}

Value IREmitter::FPDoubleToU32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDoubleToU32, {a, Imm1(round_towards_zero)});
}

Value IREmitter::FPS32ToSingle(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPS32ToSingle, {a, Imm1(round_to_nearest)});
}

Value IREmitter::FPU32ToSingle(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPU32ToSingle, {a, Imm1(round_to_nearest)});
}

Value IREmitter::FPS32ToDouble(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPS32ToDouble, {a, Imm1(round_to_nearest)});
}

Value IREmitter::FPU32ToDouble(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPU32ToDouble, {a, Imm1(round_to_nearest)});
}

void IREmitter::Breakpoint() {
    Inst(Opcode::Breakpoint, {});
}

void IREmitter::SetTerm(const Terminal& terminal) {
    block.SetTerminal(terminal);
}

Value IREmitter::Inst(Opcode op, std::initializer_list<Value> args) {
    block.AppendNewInst(op, args);
    return Value(&block.back());
}

} // namespace IR
} // namespace Dynarmic
