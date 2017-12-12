/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <initializer_list>

#include <dynarmic/coprocessor_util.h>

#include "common/common_types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/location_descriptor.h"
#include "frontend/ir/terminal.h"
#include "frontend/ir/value.h"

// ARM JIT Microinstruction Intermediate Representation
//
// This intermediate representation is an SSA IR. It is designed primarily for analysis,
// though it can be lowered into a reduced form for interpretation. Each IR node (Value)
// is a microinstruction of an idealised ARM CPU. The choice of microinstructions is made
// not based on any existing microarchitecture but on ease of implementation.

namespace Dynarmic {
namespace IR {

enum class Opcode;

/**
 * Convenience class to construct a basic block of the intermediate representation.
 * `block` is the resulting block.
 * The user of this class updates `current_location` as appropriate.
 */
class IREmitter {
public:
    explicit IREmitter(LocationDescriptor descriptor) : block(descriptor), current_location(descriptor) {}

    Block block;
    LocationDescriptor current_location;

    struct ResultAndCarry {
        Value result;
        Value carry;
    };

    struct ResultAndOverflow {
        Value result;
        Value overflow;
    };

    struct ResultAndCarryAndOverflow {
        Value result;
        Value carry;
        Value overflow;
    };

    struct ResultAndGE {
        Value result;
        Value ge;
    };

    void Unimplemented();
    u32 PC();
    u32 AlignPC(size_t alignment);

    Value Imm1(bool value);
    Value Imm8(u8 value);
    Value Imm32(u32 value);
    Value Imm64(u64 value);

    Value GetRegister(Arm::Reg source_reg);
    Value GetExtendedRegister(Arm::ExtReg source_reg);
    void SetRegister(const Arm::Reg dest_reg, const Value& value);
    void SetExtendedRegister(const Arm::ExtReg dest_reg, const Value& value);

    void ALUWritePC(const Value& value);
    void BranchWritePC(const Value& value);
    void BXWritePC(const Value& value);
    void LoadWritePC(const Value& value);
    void CallSupervisor(const Value& value);
    void PushRSB(const LocationDescriptor& return_location);

    Value GetCpsr();
    void SetCpsr(const Value& value);
    void SetCpsrNZCV(const Value& value);
    void SetCpsrNZCVQ(const Value& value);
    Value GetCFlag();
    void SetNFlag(const Value& value);
    void SetZFlag(const Value& value);
    void SetCFlag(const Value& value);
    void SetVFlag(const Value& value);
    void OrQFlag(const Value& value);
    Value GetGEFlags();
    void SetGEFlags(const Value& value);
    void SetGEFlagsCompressed(const Value& value);

    Value GetFpscr();
    void SetFpscr(const Value& new_fpscr);
    Value GetFpscrNZCV();
    void SetFpscrNZCV(const Value& new_fpscr_nzcv);

    Value Pack2x32To1x64(const Value& lo, const Value& hi);
    Value LeastSignificantWord(const Value& value);
    ResultAndCarry MostSignificantWord(const Value& value);
    Value LeastSignificantHalf(const Value& value);
    Value LeastSignificantByte(const Value& value);
    Value MostSignificantBit(const Value& value);
    Value IsZero(const Value& value);
    Value IsZero64(const Value& value);

    ResultAndCarry LogicalShiftLeft(const Value& value_in, const Value& shift_amount, const Value& carry_in);
    ResultAndCarry LogicalShiftRight(const Value& value_in, const Value& shift_amount, const Value& carry_in);
    Value LogicalShiftRight64(const Value& value_in, const Value& shift_amount);
    ResultAndCarry ArithmeticShiftRight(const Value& value_in, const Value& shift_amount, const Value& carry_in);
    ResultAndCarry RotateRight(const Value& value_in, const Value& shift_amount, const Value& carry_in);
    ResultAndCarry RotateRightExtended(const Value& value_in, const Value& carry_in);
    ResultAndCarryAndOverflow AddWithCarry(const Value& a, const Value& b, const Value& carry_in);
    Value Add(const Value& a, const Value& b);
    Value Add64(const Value& a, const Value& b);
    ResultAndCarryAndOverflow SubWithCarry(const Value& a, const Value& b, const Value& carry_in);
    Value Sub(const Value& a, const Value& b);
    Value Sub64(const Value& a, const Value& b);
    Value Mul(const Value& a, const Value& b);
    Value Mul64(const Value& a, const Value& b);
    Value And(const Value& a, const Value& b);
    Value Eor(const Value& a, const Value& b);
    Value Or(const Value& a, const Value& b);
    Value Not(const Value& a);
    Value SignExtendWordToLong(const Value& a);
    Value SignExtendHalfToWord(const Value& a);
    Value SignExtendByteToWord(const Value& a);
    Value ZeroExtendWordToLong(const Value& a);
    Value ZeroExtendHalfToWord(const Value& a);
    Value ZeroExtendByteToWord(const Value& a);
    Value ByteReverseWord(const Value& a);
    Value ByteReverseHalf(const Value& a);
    Value ByteReverseDual(const Value& a);
    Value CountLeadingZeros(const Value& a);

    ResultAndOverflow SignedSaturatedAdd(const Value& a, const Value& b);
    ResultAndOverflow SignedSaturatedSub(const Value& a, const Value& b);
    ResultAndOverflow UnsignedSaturation(const Value& a, size_t bit_size_to_saturate_to);
    ResultAndOverflow SignedSaturation(const Value& a, size_t bit_size_to_saturate_to);

    ResultAndGE PackedAddU8(const Value& a, const Value& b);
    ResultAndGE PackedAddS8(const Value& a, const Value& b);
    ResultAndGE PackedAddU16(const Value& a, const Value& b);
    ResultAndGE PackedAddS16(const Value& a, const Value& b);
    ResultAndGE PackedSubU8(const Value& a, const Value& b);
    ResultAndGE PackedSubS8(const Value& a, const Value& b);
    ResultAndGE PackedSubU16(const Value& a, const Value& b);
    ResultAndGE PackedSubS16(const Value& a, const Value& b);
    ResultAndGE PackedAddSubU16(const Value& a, const Value& b);
    ResultAndGE PackedAddSubS16(const Value& a, const Value& b);
    ResultAndGE PackedSubAddU16(const Value& a, const Value& b);
    ResultAndGE PackedSubAddS16(const Value& a, const Value& b);
    Value PackedHalvingAddU8(const Value& a, const Value& b);
    Value PackedHalvingAddS8(const Value& a, const Value& b);
    Value PackedHalvingSubU8(const Value& a, const Value& b);
    Value PackedHalvingSubS8(const Value& a, const Value& b);
    Value PackedHalvingAddU16(const Value& a, const Value& b);
    Value PackedHalvingAddS16(const Value& a, const Value& b);
    Value PackedHalvingSubU16(const Value& a, const Value& b);
    Value PackedHalvingSubS16(const Value& a, const Value& b);
    Value PackedHalvingAddSubU16(const Value& a, const Value& b);
    Value PackedHalvingAddSubS16(const Value& a, const Value& b);
    Value PackedHalvingSubAddU16(const Value& a, const Value& b);
    Value PackedHalvingSubAddS16(const Value& a, const Value& b);
    Value PackedSaturatedAddU8(const Value& a, const Value& b);
    Value PackedSaturatedAddS8(const Value& a, const Value& b);
    Value PackedSaturatedSubU8(const Value& a, const Value& b);
    Value PackedSaturatedSubS8(const Value& a, const Value& b);
    Value PackedSaturatedAddU16(const Value& a, const Value& b);
    Value PackedSaturatedAddS16(const Value& a, const Value& b);
    Value PackedSaturatedSubU16(const Value& a, const Value& b);
    Value PackedSaturatedSubS16(const Value& a, const Value& b);
    Value PackedAbsDiffSumS8(const Value& a, const Value& b);
    Value PackedSelect(const Value& ge, const Value& a, const Value& b);

    Value TransferToFP32(const Value& a);
    Value TransferToFP64(const Value& a);
    Value TransferFromFP32(const Value& a);
    Value TransferFromFP64(const Value& a);
    Value FPAbs32(const Value& a);
    Value FPAbs64(const Value& a);
    Value FPAdd32(const Value& a, const Value& b, bool fpscr_controlled);
    Value FPAdd64(const Value& a, const Value& b, bool fpscr_controlled);
    void FPCompare32(const Value& a, const Value& b, bool exc_on_qnan, bool fpscr_controlled);
    void FPCompare64(const Value& a, const Value& b, bool exc_on_qnan, bool fpscr_controlled);
    Value FPDiv32(const Value& a, const Value& b, bool fpscr_controlled);
    Value FPDiv64(const Value& a, const Value& b, bool fpscr_controlled);
    Value FPMul32(const Value& a, const Value& b, bool fpscr_controlled);
    Value FPMul64(const Value& a, const Value& b, bool fpscr_controlled);
    Value FPNeg32(const Value& a);
    Value FPNeg64(const Value& a);
    Value FPSqrt32(const Value& a);
    Value FPSqrt64(const Value& a);
    Value FPSub32(const Value& a, const Value& b, bool fpscr_controlled);
    Value FPSub64(const Value& a, const Value& b, bool fpscr_controlled);
    Value FPDoubleToSingle(const Value& a, bool fpscr_controlled);
    Value FPSingleToDouble(const Value& a, bool fpscr_controlled);
    Value FPSingleToS32(const Value& a, bool round_towards_zero, bool fpscr_controlled);
    Value FPSingleToU32(const Value& a, bool round_towards_zero, bool fpscr_controlled);
    Value FPDoubleToS32(const Value& a, bool round_towards_zero, bool fpscr_controlled);
    Value FPDoubleToU32(const Value& a, bool round_towards_zero, bool fpscr_controlled);
    Value FPS32ToSingle(const Value& a, bool round_to_nearest, bool fpscr_controlled);
    Value FPU32ToSingle(const Value& a, bool round_to_nearest, bool fpscr_controlled);
    Value FPS32ToDouble(const Value& a, bool round_to_nearest, bool fpscr_controlled);
    Value FPU32ToDouble(const Value& a, bool round_to_nearest, bool fpscr_controlled);

    void ClearExclusive();
    void SetExclusive(const Value& vaddr, size_t byte_size);
    Value ReadMemory8(const Value& vaddr);
    Value ReadMemory16(const Value& vaddr);
    Value ReadMemory32(const Value& vaddr);
    Value ReadMemory64(const Value& vaddr);
    void WriteMemory8(const Value& vaddr, const Value& value);
    void WriteMemory16(const Value& vaddr, const Value& value);
    void WriteMemory32(const Value& vaddr, const Value& value);
    void WriteMemory64(const Value& vaddr, const Value& value);
    Value ExclusiveWriteMemory8(const Value& vaddr, const Value& value);
    Value ExclusiveWriteMemory16(const Value& vaddr, const Value& value);
    Value ExclusiveWriteMemory32(const Value& vaddr, const Value& value);
    Value ExclusiveWriteMemory64(const Value& vaddr, const Value& value_lo, const Value& value_hi);

    void CoprocInternalOperation(size_t coproc_no, bool two, size_t opc1, Arm::CoprocReg CRd, Arm::CoprocReg CRn, Arm::CoprocReg CRm, size_t opc2);
    void CoprocSendOneWord(size_t coproc_no, bool two, size_t opc1, Arm::CoprocReg CRn, Arm::CoprocReg CRm, size_t opc2, const Value& word);
    void CoprocSendTwoWords(size_t coproc_no, bool two, size_t opc, Arm::CoprocReg CRm, const Value& word1, const Value& word2);
    Value CoprocGetOneWord(size_t coproc_no, bool two, size_t opc1, Arm::CoprocReg CRn, Arm::CoprocReg CRm, size_t opc2);
    Value CoprocGetTwoWords(size_t coproc_no, bool two, size_t opc, Arm::CoprocReg CRm);
    void CoprocLoadWords(size_t coproc_no, bool two, bool long_transfer, Arm::CoprocReg CRd, const Value& address, bool has_option, u8 option);
    void CoprocStoreWords(size_t coproc_no, bool two, bool long_transfer, Arm::CoprocReg CRd, const Value& address, bool has_option, u8 option);

    void Breakpoint();

    void SetTerm(const Terminal& terminal);

private:
    Value Inst(Opcode op, std::initializer_list<Value> args);
};

} // namespace IR
} // namespace Dynarmic
