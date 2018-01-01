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

void A32IREmitter::Unimplemented() {

}

u32 A32IREmitter::PC() {
    u32 offset = current_location.TFlag() ? 4 : 8;
    return current_location.PC() + offset;
}

u32 A32IREmitter::AlignPC(size_t alignment) {
    u32 pc = PC();
    return static_cast<u32>(pc - pc % alignment);
}

Value A32IREmitter::Imm1(bool imm1) {
    return Value(imm1);
}

Value A32IREmitter::Imm8(u8 imm8) {
    return Value(imm8);
}

Value A32IREmitter::Imm32(u32 imm32) {
    return Value(imm32);
}

Value A32IREmitter::Imm64(u64 imm64) {
    return Value(imm64);
}

Value A32IREmitter::GetRegister(A32::Reg reg) {
    if (reg == A32::Reg::PC) {
        return Imm32(PC());
    }
    return Inst(Opcode::GetRegister, { Value(reg) });
}

Value A32IREmitter::GetExtendedRegister(A32::ExtReg reg) {
    if (A32::IsSingleExtReg(reg)) {
        return Inst(Opcode::GetExtendedRegister32, {Value(reg)});
    }

    if (A32::IsDoubleExtReg(reg)) {
        return Inst(Opcode::GetExtendedRegister64, {Value(reg)});
    }

    ASSERT_MSG(false, "Invalid reg.");
}

void A32IREmitter::SetRegister(const A32::Reg reg, const Value& value) {
    ASSERT(reg != A32::Reg::PC);
    Inst(Opcode::SetRegister, { Value(reg), value });
}

void A32IREmitter::SetExtendedRegister(const A32::ExtReg reg, const Value& value) {
    if (A32::IsSingleExtReg(reg)) {
        Inst(Opcode::SetExtendedRegister32, {Value(reg), value});
    } else if (A32::IsDoubleExtReg(reg)) {
        Inst(Opcode::SetExtendedRegister64, {Value(reg), value});
    } else {
        ASSERT_MSG(false, "Invalid reg.");
    }
}

void A32IREmitter::ALUWritePC(const Value& value) {
    // This behaviour is ARM version-dependent.
    // The below implementation is for ARMv6k
    BranchWritePC(value);
}

void A32IREmitter::BranchWritePC(const Value& value) {
    if (!current_location.TFlag()) {
        auto new_pc = And(value, Imm32(0xFFFFFFFC));
        Inst(Opcode::SetRegister, { Value(A32::Reg::PC), new_pc });
    } else {
        auto new_pc = And(value, Imm32(0xFFFFFFFE));
        Inst(Opcode::SetRegister, { Value(A32::Reg::PC), new_pc });
    }
}

void A32IREmitter::BXWritePC(const Value& value) {
    Inst(Opcode::BXWritePC, {value});
}

void A32IREmitter::LoadWritePC(const Value& value) {
    // This behaviour is ARM version-dependent.
    // The below implementation is for ARMv6k
    BXWritePC(value);
}

void A32IREmitter::CallSupervisor(const Value& value) {
    Inst(Opcode::CallSupervisor, {value});
}

void A32IREmitter::PushRSB(const A32::LocationDescriptor& return_location) {
    Inst(Opcode::PushRSB, {Value(return_location.UniqueHash())});
}

Value A32IREmitter::GetCpsr() {
    return Inst(Opcode::GetCpsr, {});
}

void A32IREmitter::SetCpsr(const Value& value) {
    Inst(Opcode::SetCpsr, {value});
}

void A32IREmitter::SetCpsrNZCV(const Value& value) {
    Inst(Opcode::SetCpsrNZCV, {value});
}

void A32IREmitter::SetCpsrNZCVQ(const Value& value) {
    Inst(Opcode::SetCpsrNZCVQ, {value});
}

Value A32IREmitter::GetCFlag() {
    return Inst(Opcode::GetCFlag, {});
}

void A32IREmitter::SetNFlag(const Value& value) {
    Inst(Opcode::SetNFlag, {value});
}

void A32IREmitter::SetZFlag(const Value& value) {
    Inst(Opcode::SetZFlag, {value});
}

void A32IREmitter::SetCFlag(const Value& value) {
    Inst(Opcode::SetCFlag, {value});
}

void A32IREmitter::SetVFlag(const Value& value) {
    Inst(Opcode::SetVFlag, {value});
}

void A32IREmitter::OrQFlag(const Value& value) {
    Inst(Opcode::OrQFlag, {value});
}

Value A32IREmitter::GetGEFlags() {
    return Inst(Opcode::GetGEFlags, {});
}

void A32IREmitter::SetGEFlags(const Value& value) {
    Inst(Opcode::SetGEFlags, {value});
}

void A32IREmitter::SetGEFlagsCompressed(const Value& value) {
    Inst(Opcode::SetGEFlagsCompressed, {value});
}

Value A32IREmitter::GetFpscr() {
    return Inst(Opcode::GetFpscr, {});
}

void A32IREmitter::SetFpscr(const Value& new_fpscr) {
    Inst(Opcode::SetFpscr, {new_fpscr});
}

Value A32IREmitter::GetFpscrNZCV() {
    return Inst(Opcode::GetFpscrNZCV, {});
}

void A32IREmitter::SetFpscrNZCV(const Value& new_fpscr_nzcv) {
    Inst(Opcode::SetFpscrNZCV, {new_fpscr_nzcv});
}

Value A32IREmitter::Pack2x32To1x64(const Value& lo, const Value& hi) {
    return Inst(Opcode::Pack2x32To1x64, {lo, hi});
}

Value A32IREmitter::LeastSignificantWord(const Value& value) {
    return Inst(Opcode::LeastSignificantWord, {value});
}

A32IREmitter::ResultAndCarry A32IREmitter::MostSignificantWord(const Value& value) {
    auto result = Inst(Opcode::MostSignificantWord, {value});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

Value A32IREmitter::LeastSignificantHalf(const Value& value) {
    return Inst(Opcode::LeastSignificantHalf, {value});
}

Value A32IREmitter::LeastSignificantByte(const Value& value) {
    return Inst(Opcode::LeastSignificantByte, {value});
}

Value A32IREmitter::MostSignificantBit(const Value& value) {
    return Inst(Opcode::MostSignificantBit, {value});
}

Value A32IREmitter::IsZero(const Value& value) {
    return Inst(Opcode::IsZero, {value});
}

Value A32IREmitter::IsZero64(const Value& value) {
    return Inst(Opcode::IsZero64, {value});
}

A32IREmitter::ResultAndCarry A32IREmitter::LogicalShiftLeft(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::LogicalShiftLeft, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

A32IREmitter::ResultAndCarry A32IREmitter::LogicalShiftRight(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::LogicalShiftRight, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

Value A32IREmitter::LogicalShiftRight64(const Value& value_in, const Value& shift_amount) {
    return Inst(Opcode::LogicalShiftRight64, {value_in, shift_amount});
}

A32IREmitter::ResultAndCarry A32IREmitter::ArithmeticShiftRight(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::ArithmeticShiftRight, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

A32IREmitter::ResultAndCarry A32IREmitter::RotateRight(const Value& value_in, const Value& shift_amount, const Value& carry_in) {
    auto result = Inst(Opcode::RotateRight, {value_in, shift_amount, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

A32IREmitter::ResultAndCarry A32IREmitter::RotateRightExtended(const Value& value_in, const Value& carry_in) {
    auto result = Inst(Opcode::RotateRightExtended, {value_in, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    return {result, carry_out};
}

A32IREmitter::ResultAndCarryAndOverflow A32IREmitter::AddWithCarry(const Value& a, const Value& b, const Value& carry_in) {
    auto result = Inst(Opcode::AddWithCarry, {a, b, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, carry_out, overflow};
}

Value A32IREmitter::Add(const Value& a, const Value& b) {
    return Inst(Opcode::AddWithCarry, {a, b, Imm1(0)});
}

Value A32IREmitter::Add64(const Value& a, const Value& b) {
    return Inst(Opcode::Add64, {a, b});
}

A32IREmitter::ResultAndCarryAndOverflow A32IREmitter::SubWithCarry(const Value& a, const Value& b, const Value& carry_in) {
    // This is equivalent to AddWithCarry(a, Not(b), carry_in).
    auto result = Inst(Opcode::SubWithCarry, {a, b, carry_in});
    auto carry_out = Inst(Opcode::GetCarryFromOp, {result});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, carry_out, overflow};
}

Value A32IREmitter::Sub(const Value& a, const Value& b) {
    return Inst(Opcode::SubWithCarry, {a, b, Imm1(1)});
}

Value A32IREmitter::Sub64(const Value& a, const Value& b) {
    return Inst(Opcode::Sub64, {a, b});
}

Value A32IREmitter::Mul(const Value& a, const Value& b) {
    return Inst(Opcode::Mul, {a, b});
}

Value A32IREmitter::Mul64(const Value& a, const Value& b) {
    return Inst(Opcode::Mul64, {a, b});
}

Value A32IREmitter::And(const Value& a, const Value& b) {
    return Inst(Opcode::And, {a, b});
}

Value A32IREmitter::Eor(const Value& a, const Value& b) {
    return Inst(Opcode::Eor, {a, b});
}

Value A32IREmitter::Or(const Value& a, const Value& b) {
    return Inst(Opcode::Or, {a, b});
}

Value A32IREmitter::Not(const Value& a) {
    return Inst(Opcode::Not, {a});
}

Value A32IREmitter::SignExtendWordToLong(const Value& a) {
    return Inst(Opcode::SignExtendWordToLong, {a});
}

Value A32IREmitter::SignExtendHalfToWord(const Value& a) {
    return Inst(Opcode::SignExtendHalfToWord, {a});
}

Value A32IREmitter::SignExtendByteToWord(const Value& a) {
    return Inst(Opcode::SignExtendByteToWord, {a});
}

Value A32IREmitter::ZeroExtendWordToLong(const Value& a) {
    return Inst(Opcode::ZeroExtendWordToLong, {a});
}

Value A32IREmitter::ZeroExtendHalfToWord(const Value& a) {
    return Inst(Opcode::ZeroExtendHalfToWord, {a});
}

Value A32IREmitter::ZeroExtendByteToWord(const Value& a) {
    return Inst(Opcode::ZeroExtendByteToWord, {a});
}

Value A32IREmitter::ByteReverseWord(const Value& a) {
    return Inst(Opcode::ByteReverseWord, {a});
}

Value A32IREmitter::ByteReverseHalf(const Value& a) {
    return Inst(Opcode::ByteReverseHalf, {a});
}

Value A32IREmitter::ByteReverseDual(const Value& a) {
    return Inst(Opcode::ByteReverseDual, {a});
}

Value A32IREmitter::CountLeadingZeros(const Value& a) {
    return Inst(Opcode::CountLeadingZeros, {a});
}

A32IREmitter::ResultAndOverflow A32IREmitter::SignedSaturatedAdd(const Value& a, const Value& b) {
    auto result = Inst(Opcode::SignedSaturatedAdd, {a, b});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

A32IREmitter::ResultAndOverflow A32IREmitter::SignedSaturatedSub(const Value& a, const Value& b) {
    auto result = Inst(Opcode::SignedSaturatedSub, {a, b});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

A32IREmitter::ResultAndOverflow A32IREmitter::UnsignedSaturation(const Value& a, size_t bit_size_to_saturate_to) {
    ASSERT(bit_size_to_saturate_to <= 31);
    auto result = Inst(Opcode::UnsignedSaturation, {a, Imm8(static_cast<u8>(bit_size_to_saturate_to))});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

A32IREmitter::ResultAndOverflow A32IREmitter::SignedSaturation(const Value& a, size_t bit_size_to_saturate_to) {
    ASSERT(bit_size_to_saturate_to >= 1 && bit_size_to_saturate_to <= 32);
    auto result = Inst(Opcode::SignedSaturation, {a, Imm8(static_cast<u8>(bit_size_to_saturate_to))});
    auto overflow = Inst(Opcode::GetOverflowFromOp, {result});
    return {result, overflow};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedAddU8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddU8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedAddS8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddS8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedAddU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedAddS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedSubU8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubU8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedSubS8(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubS8, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedSubU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedSubS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedAddSubU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddSubU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedAddSubS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedAddSubS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedSubAddU16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubAddU16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

A32IREmitter::ResultAndGE A32IREmitter::PackedSubAddS16(const Value& a, const Value& b) {
    auto result = Inst(Opcode::PackedSubAddS16, {a, b});
    auto ge = Inst(Opcode::GetGEFromOp, {result});
    return {result, ge};
}

Value A32IREmitter::PackedHalvingAddU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddU8, {a, b});
}

Value A32IREmitter::PackedHalvingAddS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddS8, {a, b});
}

Value A32IREmitter::PackedHalvingSubU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubU8, {a, b});
}

Value A32IREmitter::PackedHalvingSubS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubS8, {a, b});
}

Value A32IREmitter::PackedHalvingAddU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddU16, {a, b});
}

Value A32IREmitter::PackedHalvingAddS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddS16, {a, b});
}

Value A32IREmitter::PackedHalvingSubU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubU16, {a, b});
}

Value A32IREmitter::PackedHalvingSubS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubS16, {a, b});
}

Value A32IREmitter::PackedHalvingAddSubU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddSubU16, {a, b});
}

Value A32IREmitter::PackedHalvingAddSubS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingAddSubS16, {a, b});
}

Value A32IREmitter::PackedHalvingSubAddU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubAddU16, {a, b});
}

Value A32IREmitter::PackedHalvingSubAddS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedHalvingSubAddS16, {a, b});
}

Value A32IREmitter::PackedSaturatedAddU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddU8, {a, b});
}

Value A32IREmitter::PackedSaturatedAddS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddS8, {a, b});
}

Value A32IREmitter::PackedSaturatedSubU8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubU8, {a, b});
}

Value A32IREmitter::PackedSaturatedSubS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubS8, {a, b});
}

Value A32IREmitter::PackedSaturatedAddU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddU16, {a, b});
}

Value A32IREmitter::PackedSaturatedAddS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedAddS16, {a, b});
}

Value A32IREmitter::PackedSaturatedSubU16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubU16, {a, b});
}

Value A32IREmitter::PackedSaturatedSubS16(const Value& a, const Value& b) {
    return Inst(Opcode::PackedSaturatedSubS16, {a, b});
}

Value A32IREmitter::PackedAbsDiffSumS8(const Value& a, const Value& b) {
    return Inst(Opcode::PackedAbsDiffSumS8, {a, b});
}

Value A32IREmitter::PackedSelect(const Value& ge, const Value& a, const Value& b) {
    return Inst(Opcode::PackedSelect, {ge, a, b});
}

Value A32IREmitter::TransferToFP32(const Value& a) {
    return Inst(Opcode::TransferToFP32, {a});
}

Value A32IREmitter::TransferToFP64(const Value& a) {
    return Inst(Opcode::TransferToFP64, {a});
}

Value A32IREmitter::TransferFromFP32(const Value& a) {
    return Inst(Opcode::TransferFromFP32, {a});
}

Value A32IREmitter::TransferFromFP64(const Value& a) {
    return Inst(Opcode::TransferFromFP64, {a});
}

Value A32IREmitter::FPAbs32(const Value& a) {
    return Inst(Opcode::FPAbs32, {a});
}

Value A32IREmitter::FPAbs64(const Value& a) {
    return Inst(Opcode::FPAbs64, {a});
}

Value A32IREmitter::FPAdd32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPAdd32, {a, b});
}

Value A32IREmitter::FPAdd64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPAdd64, {a, b});
}

void A32IREmitter::FPCompare32(const Value& a, const Value& b, bool exc_on_qnan, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    Inst(Opcode::FPCompare32, {a, b, Imm1(exc_on_qnan)});
}

void A32IREmitter::FPCompare64(const Value& a, const Value& b, bool exc_on_qnan, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    Inst(Opcode::FPCompare64, {a, b, Imm1(exc_on_qnan)});
}

Value A32IREmitter::FPDiv32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDiv32, {a, b});
}

Value A32IREmitter::FPDiv64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDiv64, {a, b});
}

Value A32IREmitter::FPMul32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPMul32, {a, b});
}

Value A32IREmitter::FPMul64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPMul64, {a, b});
}

Value A32IREmitter::FPNeg32(const Value& a) {
    return Inst(Opcode::FPNeg32, {a});
}

Value A32IREmitter::FPNeg64(const Value& a) {
    return Inst(Opcode::FPNeg64, {a});
}

Value A32IREmitter::FPSqrt32(const Value& a) {
    return Inst(Opcode::FPSqrt32, {a});
}

Value A32IREmitter::FPSqrt64(const Value& a) {
    return Inst(Opcode::FPSqrt64, {a});
}

Value A32IREmitter::FPSub32(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSub32, {a, b});
}

Value A32IREmitter::FPSub64(const Value& a, const Value& b, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSub64, {a, b});
}

Value A32IREmitter::FPDoubleToSingle(const Value& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDoubleToSingle, {a});
}

Value A32IREmitter::FPSingleToDouble(const Value& a, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSingleToDouble, {a});
}

Value A32IREmitter::FPSingleToS32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSingleToS32, {a, Imm1(round_towards_zero)});
}

Value A32IREmitter::FPSingleToU32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPSingleToU32, {a, Imm1(round_towards_zero)});
}

Value A32IREmitter::FPDoubleToS32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDoubleToS32, {a, Imm1(round_towards_zero)});
}

Value A32IREmitter::FPDoubleToU32(const Value& a, bool round_towards_zero, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPDoubleToU32, {a, Imm1(round_towards_zero)});
}

Value A32IREmitter::FPS32ToSingle(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPS32ToSingle, {a, Imm1(round_to_nearest)});
}

Value A32IREmitter::FPU32ToSingle(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPU32ToSingle, {a, Imm1(round_to_nearest)});
}

Value A32IREmitter::FPS32ToDouble(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPS32ToDouble, {a, Imm1(round_to_nearest)});
}

Value A32IREmitter::FPU32ToDouble(const Value& a, bool round_to_nearest, bool fpscr_controlled) {
    ASSERT(fpscr_controlled);
    return Inst(Opcode::FPU32ToDouble, {a, Imm1(round_to_nearest)});
}

void A32IREmitter::ClearExclusive() {
    Inst(Opcode::ClearExclusive, {});
}

void A32IREmitter::SetExclusive(const Value& vaddr, size_t byte_size) {
    ASSERT(byte_size == 1 || byte_size == 2 || byte_size == 4 || byte_size == 8 || byte_size == 16);
    Inst(Opcode::SetExclusive, {vaddr, Imm8(u8(byte_size))});
}

Value A32IREmitter::ReadMemory8(const Value& vaddr) {
    return Inst(Opcode::ReadMemory8, {vaddr});
}

Value A32IREmitter::ReadMemory16(const Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory16, {vaddr});
    return current_location.EFlag() ? ByteReverseHalf(value) : value;
}

Value A32IREmitter::ReadMemory32(const Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory32, {vaddr});
    return current_location.EFlag() ? ByteReverseWord(value) : value;
}

Value A32IREmitter::ReadMemory64(const Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory64, {vaddr});
    return current_location.EFlag() ? ByteReverseDual(value) : value;
}

void A32IREmitter::WriteMemory8(const Value& vaddr, const Value& value) {
    Inst(Opcode::WriteMemory8, {vaddr, value});
}

void A32IREmitter::WriteMemory16(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseHalf(value);
        Inst(Opcode::WriteMemory16, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory16, {vaddr, value});
    }
}

void A32IREmitter::WriteMemory32(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseWord(value);
        Inst(Opcode::WriteMemory32, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory32, {vaddr, value});
    }
}

void A32IREmitter::WriteMemory64(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseDual(value);
        Inst(Opcode::WriteMemory64, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory64, {vaddr, value});
    }
}

Value A32IREmitter::ExclusiveWriteMemory8(const Value& vaddr, const Value& value) {
    return Inst(Opcode::ExclusiveWriteMemory8, {vaddr, value});
}

Value A32IREmitter::ExclusiveWriteMemory16(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseHalf(value);
        return Inst(Opcode::ExclusiveWriteMemory16, {vaddr, v});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory16, {vaddr, value});
    }
}

Value A32IREmitter::ExclusiveWriteMemory32(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseWord(value);
        return Inst(Opcode::ExclusiveWriteMemory32, {vaddr, v});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory32, {vaddr, value});
    }
}

Value A32IREmitter::ExclusiveWriteMemory64(const Value& vaddr, const Value& value_lo, const Value& value_hi) {
    if (current_location.EFlag()) {
        auto vlo = ByteReverseWord(value_lo);
        auto vhi = ByteReverseWord(value_hi);
        return Inst(Opcode::ExclusiveWriteMemory64, {vaddr, vlo, vhi});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory64, {vaddr, value_lo, value_hi});
    }
}

void A32IREmitter::CoprocInternalOperation(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRd, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    Inst(Opcode::CoprocInternalOperation, {Value(coproc_info)});
}

void A32IREmitter::CoprocSendOneWord(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2, const Value& word) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    Inst(Opcode::CoprocSendOneWord, {Value(coproc_info), word});
}

void A32IREmitter::CoprocSendTwoWords(size_t coproc_no, bool two, size_t opc, A32::CoprocReg CRm, const Value& word1, const Value& word2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc),
                                  static_cast<u8>(CRm)};
    Inst(Opcode::CoprocSendTwoWords, {Value(coproc_info), word1, word2});
}

Value A32IREmitter::CoprocGetOneWord(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    return Inst(Opcode::CoprocGetOneWord, {Value(coproc_info)});
}

Value A32IREmitter::CoprocGetTwoWords(size_t coproc_no, bool two, size_t opc, A32::CoprocReg CRm) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc),
                                  static_cast<u8>(CRm)};
    return Inst(Opcode::CoprocGetTwoWords, {Value(coproc_info)});
}

void A32IREmitter::CoprocLoadWords(size_t coproc_no, bool two, bool long_transfer, A32::CoprocReg CRd, const Value& address, bool has_option, u8 option) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(long_transfer ? 1 : 0),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(has_option ? 1 : 0),
                                  static_cast<u8>(option)};
    Inst(Opcode::CoprocLoadWords, {Value(coproc_info), address});
}

void A32IREmitter::CoprocStoreWords(size_t coproc_no, bool two, bool long_transfer, A32::CoprocReg CRd, const Value& address, bool has_option, u8 option) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(long_transfer ? 1 : 0),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(has_option ? 1 : 0),
                                  static_cast<u8>(option)};
    Inst(Opcode::CoprocStoreWords, {Value(coproc_info), address});
}

void A32IREmitter::Breakpoint() {
    Inst(Opcode::Breakpoint, {});
}

void A32IREmitter::SetTerm(const Terminal& terminal) {
    block.SetTerminal(terminal);
}

Value A32IREmitter::Inst(Opcode op, std::initializer_list<Value> args) {
    block.AppendNewInst(op, args);
    return Value(&block.back());
}

} // namespace IR
} // namespace Dynarmic
