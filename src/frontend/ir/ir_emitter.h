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

/**
 * Convenience class to construct a basic block of the intermediate representation.
 * `block` is the resulting block.
 * The user of this class updates `current_location` as appropriate.
 */
class IREmitter {
public:
    explicit IREmitter(IR::LocationDescriptor descriptor) : block(descriptor) {}

    Block block;

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

    Value Imm1(bool value);
    Value Imm8(u8 value);
    Value Imm32(u32 value);
    Value Imm64(u64 value);

    void PushRSB(const LocationDescriptor& return_location);

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

    void Breakpoint();

    void SetTerm(const Terminal& terminal);

protected:
    Value Inst(Opcode op, std::initializer_list<Value> args);
};

} // namespace IR
} // namespace Dynarmic
