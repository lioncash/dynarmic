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

u32 IREmitter::PC() {
    u32 offset = current_location.TFlag() ? 4 : 8;
    return current_location.PC() + offset;
}

u32 IREmitter::AlignPC(size_t alignment) {
    u32 pc = PC();
    return static_cast<u32>(pc - pc % alignment);
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

Value IREmitter::GetRegister(Arm::Reg reg) {
    if (reg == Arm::Reg::PC) {
        return Imm32(PC());
    }
    return Inst(Opcode::GetRegister, { Value(reg) });
}

Value IREmitter::GetExtendedRegister(Arm::ExtReg reg) {
    if (Arm::IsSingleExtReg(reg)) {
        return Inst(Opcode::GetExtendedRegister32, {Value(reg)});
    }

    if (Arm::IsDoubleExtReg(reg)) {
        return Inst(Opcode::GetExtendedRegister64, {Value(reg)});
    }

    ASSERT_MSG(false, "Invalid reg.");
}

void IREmitter::SetRegister(const Arm::Reg reg, const Value& value) {
    ASSERT(reg != Arm::Reg::PC);
    Inst(Opcode::SetRegister, { Value(reg), value });
}

void IREmitter::SetExtendedRegister(const Arm::ExtReg reg, const Value& value) {
    if (Arm::IsSingleExtReg(reg)) {
        Inst(Opcode::SetExtendedRegister32, {Value(reg), value});
    } else if (Arm::IsDoubleExtReg(reg)) {
        Inst(Opcode::SetExtendedRegister64, {Value(reg), value});
    } else {
        ASSERT_MSG(false, "Invalid reg.");
    }
}

void IREmitter::ALUWritePC(const Value& value) {
    // This behaviour is ARM version-dependent.
    // The below implementation is for ARMv6k
    BranchWritePC(value);
}

void IREmitter::BranchWritePC(const Value& value) {
    if (!current_location.TFlag()) {
        auto new_pc = And(value, Imm32(0xFFFFFFFC));
        Inst(Opcode::SetRegister, { Value(Arm::Reg::PC), new_pc });
    } else {
        auto new_pc = And(value, Imm32(0xFFFFFFFE));
        Inst(Opcode::SetRegister, { Value(Arm::Reg::PC), new_pc });
    }
}

void IREmitter::BXWritePC(const Value& value) {
    Inst(Opcode::BXWritePC, {value});
}

void IREmitter::LoadWritePC(const Value& value) {
    // This behaviour is ARM version-dependent.
    // The below implementation is for ARMv6k
    BXWritePC(value);
}

void IREmitter::CallSupervisor(const Value& value) {
    Inst(Opcode::CallSupervisor, {value});
}

void IREmitter::PushRSB(const LocationDescriptor& return_location) {
    Inst(Opcode::PushRSB, {Value(return_location.UniqueHash())});
}

Value IREmitter::GetCpsr() {
    return Inst(Opcode::GetCpsr, {});
}

void IREmitter::SetCpsr(const Value& value) {
    Inst(Opcode::SetCpsr, {value});
}

void IREmitter::SetCpsrNZCV(const Value& value) {
    Inst(Opcode::SetCpsrNZCV, {value});
}

void IREmitter::SetCpsrNZCVQ(const Value& value) {
    Inst(Opcode::SetCpsrNZCVQ, {value});
}

Value IREmitter::GetCFlag() {
    return Inst(Opcode::GetCFlag, {});
}

void IREmitter::SetNFlag(const Value& value) {
    Inst(Opcode::SetNFlag, {value});
}

void IREmitter::SetZFlag(const Value& value) {
    Inst(Opcode::SetZFlag, {value});
}

void IREmitter::SetCFlag(const Value& value) {
    Inst(Opcode::SetCFlag, {value});
}

void IREmitter::SetVFlag(const Value& value) {
    Inst(Opcode::SetVFlag, {value});
}

void IREmitter::OrQFlag(const Value& value) {
    Inst(Opcode::OrQFlag, {value});
}

Value IREmitter::GetGEFlags() {
    return Inst(Opcode::GetGEFlags, {});
}

void IREmitter::SetGEFlags(const Value& value) {
    Inst(Opcode::SetGEFlags, {value});
}

void IREmitter::SetGEFlagsCompressed(const Value& value) {
    Inst(Opcode::SetGEFlagsCompressed, {value});
}

Value IREmitter::GetFpscr() {
    return Inst(Opcode::GetFpscr, {});
}

void IREmitter::SetFpscr(const Value& new_fpscr) {
    Inst(Opcode::SetFpscr, {new_fpscr});
}

Value IREmitter::GetFpscrNZCV() {
    return Inst(Opcode::GetFpscrNZCV, {});
}

void IREmitter::SetFpscrNZCV(const Value& new_fpscr_nzcv) {
    Inst(Opcode::SetFpscrNZCV, {new_fpscr_nzcv});
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

void IREmitter::ClearExclusive() {
    Inst(Opcode::ClearExclusive, {});
}

void IREmitter::SetExclusive(const Value& vaddr, size_t byte_size) {
    ASSERT(byte_size == 1 || byte_size == 2 || byte_size == 4 || byte_size == 8 || byte_size == 16);
    Inst(Opcode::SetExclusive, {vaddr, Imm8(u8(byte_size))});
}

Value IREmitter::ReadMemory8(const Value& vaddr) {
    return Inst(Opcode::ReadMemory8, {vaddr});
}

Value IREmitter::ReadMemory16(const Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory16, {vaddr});
    return current_location.EFlag() ? ByteReverseHalf(value) : value;
}

Value IREmitter::ReadMemory32(const Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory32, {vaddr});
    return current_location.EFlag() ? ByteReverseWord(value) : value;
}

Value IREmitter::ReadMemory64(const Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory64, {vaddr});
    return current_location.EFlag() ? ByteReverseDual(value) : value;
}

void IREmitter::WriteMemory8(const Value& vaddr, const Value& value) {
    Inst(Opcode::WriteMemory8, {vaddr, value});
}

void IREmitter::WriteMemory16(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseHalf(value);
        Inst(Opcode::WriteMemory16, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory16, {vaddr, value});
    }
}

void IREmitter::WriteMemory32(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseWord(value);
        Inst(Opcode::WriteMemory32, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory32, {vaddr, value});
    }
}

void IREmitter::WriteMemory64(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseDual(value);
        Inst(Opcode::WriteMemory64, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory64, {vaddr, value});
    }
}

Value IREmitter::ExclusiveWriteMemory8(const Value& vaddr, const Value& value) {
    return Inst(Opcode::ExclusiveWriteMemory8, {vaddr, value});
}

Value IREmitter::ExclusiveWriteMemory16(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseHalf(value);
        return Inst(Opcode::ExclusiveWriteMemory16, {vaddr, v});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory16, {vaddr, value});
    }
}

Value IREmitter::ExclusiveWriteMemory32(const Value& vaddr, const Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseWord(value);
        return Inst(Opcode::ExclusiveWriteMemory32, {vaddr, v});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory32, {vaddr, value});
    }
}

Value IREmitter::ExclusiveWriteMemory64(const Value& vaddr, const Value& value_lo, const Value& value_hi) {
    if (current_location.EFlag()) {
        auto vlo = ByteReverseWord(value_lo);
        auto vhi = ByteReverseWord(value_hi);
        return Inst(Opcode::ExclusiveWriteMemory64, {vaddr, vlo, vhi});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory64, {vaddr, value_lo, value_hi});
    }
}

void IREmitter::CoprocInternalOperation(size_t coproc_no, bool two, size_t opc1, Arm::CoprocReg CRd, Arm::CoprocReg CRn, Arm::CoprocReg CRm, size_t opc2) {
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

void IREmitter::CoprocSendOneWord(size_t coproc_no, bool two, size_t opc1, Arm::CoprocReg CRn, Arm::CoprocReg CRm, size_t opc2, const Value& word) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    Inst(Opcode::CoprocSendOneWord, {Value(coproc_info), word});
}

void IREmitter::CoprocSendTwoWords(size_t coproc_no, bool two, size_t opc, Arm::CoprocReg CRm, const Value& word1, const Value& word2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc),
                                  static_cast<u8>(CRm)};
    Inst(Opcode::CoprocSendTwoWords, {Value(coproc_info), word1, word2});
}

Value IREmitter::CoprocGetOneWord(size_t coproc_no, bool two, size_t opc1, Arm::CoprocReg CRn, Arm::CoprocReg CRm, size_t opc2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    return Inst(Opcode::CoprocGetOneWord, {Value(coproc_info)});
}

Value IREmitter::CoprocGetTwoWords(size_t coproc_no, bool two, size_t opc, Arm::CoprocReg CRm) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc),
                                  static_cast<u8>(CRm)};
    return Inst(Opcode::CoprocGetTwoWords, {Value(coproc_info)});
}

void IREmitter::CoprocLoadWords(size_t coproc_no, bool two, bool long_transfer, Arm::CoprocReg CRd, const Value& address, bool has_option, u8 option) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(long_transfer ? 1 : 0),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(has_option ? 1 : 0),
                                  static_cast<u8>(option)};
    Inst(Opcode::CoprocLoadWords, {Value(coproc_info), address});
}

void IREmitter::CoprocStoreWords(size_t coproc_no, bool two, bool long_transfer, Arm::CoprocReg CRd, const Value& address, bool has_option, u8 option) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(long_transfer ? 1 : 0),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(has_option ? 1 : 0),
                                  static_cast<u8>(option)};
    Inst(Opcode::CoprocStoreWords, {Value(coproc_info), address});
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
