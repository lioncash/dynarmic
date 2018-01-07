/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <initializer_list>

#include <dynarmic/A32/coprocessor_util.h>

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

template <typename T>
struct ResultAndCarry {
    T result;
    U1 carry;
};

template <typename T>
struct ResultAndOverflow {
    T result;
    U1 overflow;
};

template <typename T>
struct ResultAndCarryAndOverflow {
    T result;
    U1 carry;
    U1 overflow;
};

template <typename T>
struct ResultAndGE {
    T result;
    U32 ge;
};

/**
 * Convenience class to construct a basic block of the intermediate representation.
 * `block` is the resulting block.
 * The user of this class updates `current_location` as appropriate.
 */
class IREmitter {
public:
    explicit IREmitter(IR::LocationDescriptor descriptor) : block(descriptor) {}

    Block block;

    void Unimplemented();

    U1 Imm1(bool value);
    U8 Imm8(u8 value);
    U32 Imm32(u32 value);
    U64 Imm64(u64 value);

    void PushRSB(const LocationDescriptor& return_location);

    U64 Pack2x32To1x64(const U32& lo, const U32& hi);
    U32 LeastSignificantWord(const U64& value);
    ResultAndCarry<U32> MostSignificantWord(const U64& value);
    U16 LeastSignificantHalf(U32U64 value);
    U8 LeastSignificantByte(U32U64 value);
    U1 MostSignificantBit(const U32& value);
    U1 IsZero(const U32& value);
    U1 IsZero64(const U64& value);

    // This pseudo-instruction may only be added to instructions that support it.
    NZCV NZCVFrom(const Value& value);

    ResultAndCarry<U32> LogicalShiftLeft(const U32& value_in, const U8& shift_amount, const U1& carry_in);
    ResultAndCarry<U32> LogicalShiftRight(const U32& value_in, const U8& shift_amount, const U1& carry_in);
    ResultAndCarry<U32> ArithmeticShiftRight(const U32& value_in, const U8& shift_amount, const U1& carry_in);
    ResultAndCarry<U32> RotateRight(const U32& value_in, const U8& shift_amount, const U1& carry_in);
    U64 LogicalShiftRight(const U64& value_in, const U8& shift_amount);
    U32U64 LogicalShiftLeft(const U32U64& value_in, const U8& shift_amount);
    U32U64 LogicalShiftRight(const U32U64& value_in, const U8& shift_amount);
    U32U64 ArithmeticShiftRight(const U32U64& value_in, const U8& shift_amount);
    U32U64 RotateRight(const U32U64& value_in, const U8& shift_amount);
    ResultAndCarry<U32> RotateRightExtended(const U32& value_in, const U1& carry_in);
    ResultAndCarryAndOverflow<U32> AddWithCarry(const U32& a, const U32& b, const U1& carry_in);
    ResultAndCarryAndOverflow<U32> SubWithCarry(const U32& a, const U32& b, const U1& carry_in);
    U32U64 AddWithCarry(const U32U64& a, const U32U64& b, const U1& carry_in);
    U32U64 SubWithCarry(const U32U64& a, const U32U64& b, const U1& carry_in);
    U32 Add(const U32& a, const U32& b);
    U64 Add(const U64& a, const U64& b);
    U32U64 Add(const U32U64& a, const U32U64& b);
    U32 Sub(const U32& a, const U32& b);
    U64 Sub(const U64& a, const U64& b);
    U32U64 Sub(const U32U64& a, const U32U64& b);
    U32 Mul(const U32& a, const U32& b);
    U64 Mul(const U64& a, const U64& b);
    U32 And(const U32& a, const U32& b);
    U32 Eor(const U32& a, const U32& b);
    U32 Or(const U32& a, const U32& b);
    U32 Not(const U32& a);
    U32 SignExtendToWord(const UAny& a);
    U64 SignExtendToLong(const UAny& a);
    U32 SignExtendByteToWord(const U8& a);
    U32 SignExtendHalfToWord(const U16& a);
    U64 SignExtendWordToLong(const U32& a);
    U32 ZeroExtendToWord(const UAny& a);
    U64 ZeroExtendToLong(const UAny& a);
    U32 ZeroExtendByteToWord(const U8& a);
    U32 ZeroExtendHalfToWord(const U16& a);
    U64 ZeroExtendWordToLong(const U32& a);
    U32 ByteReverseWord(const U32& a);
    U16 ByteReverseHalf(const U16& a);
    U64 ByteReverseDual(const U64& a);
    U32 CountLeadingZeros(const U32& a);

    ResultAndOverflow<U32> SignedSaturatedAdd(const U32& a, const U32& b);
    ResultAndOverflow<U32> SignedSaturatedSub(const U32& a, const U32& b);
    ResultAndOverflow<U32> UnsignedSaturation(const U32& a, size_t bit_size_to_saturate_to);
    ResultAndOverflow<U32> SignedSaturation(const U32& a, size_t bit_size_to_saturate_to);

    ResultAndGE<U32> PackedAddU8(const U32& a, const U32& b);
    ResultAndGE<U32> PackedAddS8(const U32& a, const U32& b);
    ResultAndGE<U32> PackedAddU16(const U32& a, const U32& b);
    ResultAndGE<U32> PackedAddS16(const U32& a, const U32& b);
    ResultAndGE<U32> PackedSubU8(const U32& a, const U32& b);
    ResultAndGE<U32> PackedSubS8(const U32& a, const U32& b);
    ResultAndGE<U32> PackedSubU16(const U32& a, const U32& b);
    ResultAndGE<U32> PackedSubS16(const U32& a, const U32& b);
    ResultAndGE<U32> PackedAddSubU16(const U32& a, const U32& b);
    ResultAndGE<U32> PackedAddSubS16(const U32& a, const U32& b);
    ResultAndGE<U32> PackedSubAddU16(const U32& a, const U32& b);
    ResultAndGE<U32> PackedSubAddS16(const U32& a, const U32& b);
    U32 PackedHalvingAddU8(const U32& a, const U32& b);
    U32 PackedHalvingAddS8(const U32& a, const U32& b);
    U32 PackedHalvingSubU8(const U32& a, const U32& b);
    U32 PackedHalvingSubS8(const U32& a, const U32& b);
    U32 PackedHalvingAddU16(const U32& a, const U32& b);
    U32 PackedHalvingAddS16(const U32& a, const U32& b);
    U32 PackedHalvingSubU16(const U32& a, const U32& b);
    U32 PackedHalvingSubS16(const U32& a, const U32& b);
    U32 PackedHalvingAddSubU16(const U32& a, const U32& b);
    U32 PackedHalvingAddSubS16(const U32& a, const U32& b);
    U32 PackedHalvingSubAddU16(const U32& a, const U32& b);
    U32 PackedHalvingSubAddS16(const U32& a, const U32& b);
    U32 PackedSaturatedAddU8(const U32& a, const U32& b);
    U32 PackedSaturatedAddS8(const U32& a, const U32& b);
    U32 PackedSaturatedSubU8(const U32& a, const U32& b);
    U32 PackedSaturatedSubS8(const U32& a, const U32& b);
    U32 PackedSaturatedAddU16(const U32& a, const U32& b);
    U32 PackedSaturatedAddS16(const U32& a, const U32& b);
    U32 PackedSaturatedSubU16(const U32& a, const U32& b);
    U32 PackedSaturatedSubS16(const U32& a, const U32& b);
    U32 PackedAbsDiffSumS8(const U32& a, const U32& b);
    U32 PackedSelect(const U32& ge, const U32& a, const U32& b);

    F32 TransferToFP32(const U32& a);
    F64 TransferToFP64(const U64& a);
    U32 TransferFromFP32(const F32& a);
    U64 TransferFromFP64(const F64& a);
    F32 FPAbs32(const F32& a);
    F64 FPAbs64(const F64& a);
    F32 FPAdd32(const F32& a, const F32& b, bool fpscr_controlled);
    F64 FPAdd64(const F64& a, const F64& b, bool fpscr_controlled);
    void FPCompare32(const F32& a, const F32& b, bool exc_on_qnan, bool fpscr_controlled);
    void FPCompare64(const F64& a, const F64& b, bool exc_on_qnan, bool fpscr_controlled);
    F32 FPDiv32(const F32& a, const F32& b, bool fpscr_controlled);
    F64 FPDiv64(const F64& a, const F64& b, bool fpscr_controlled);
    F32 FPMul32(const F32& a, const F32& b, bool fpscr_controlled);
    F64 FPMul64(const F64& a, const F64& b, bool fpscr_controlled);
    F32 FPNeg32(const F32& a);
    F64 FPNeg64(const F64& a);
    F32 FPSqrt32(const F32& a);
    F64 FPSqrt64(const F64& a);
    F32 FPSub32(const F32& a, const F32& b, bool fpscr_controlled);
    F64 FPSub64(const F64& a, const F64& b, bool fpscr_controlled);
    F32 FPDoubleToSingle(const F64& a, bool fpscr_controlled);
    F64 FPSingleToDouble(const F32& a, bool fpscr_controlled);
    F32 FPSingleToS32(const F32& a, bool round_towards_zero, bool fpscr_controlled);
    F32 FPSingleToU32(const F32& a, bool round_towards_zero, bool fpscr_controlled);
    F32 FPDoubleToS32(const F32& a, bool round_towards_zero, bool fpscr_controlled);
    F32 FPDoubleToU32(const F32& a, bool round_towards_zero, bool fpscr_controlled);
    F32 FPS32ToSingle(const F32& a, bool round_to_nearest, bool fpscr_controlled);
    F32 FPU32ToSingle(const F32& a, bool round_to_nearest, bool fpscr_controlled);
    F64 FPS32ToDouble(const F32& a, bool round_to_nearest, bool fpscr_controlled);
    F64 FPU32ToDouble(const F32& a, bool round_to_nearest, bool fpscr_controlled);

    void Breakpoint();

    void SetTerm(const Terminal& terminal);

protected:
    template<typename T = Value, typename ...Args>
    T Inst(Opcode op, Args ...args) {
        block.AppendNewInst(op, {Value(args)...});
        return T(Value(&block.back()));
    }
};

} // namespace IR
} // namespace Dynarmic
