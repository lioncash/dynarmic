/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::REV(bool sf, bool opc_0, Reg Rn, Reg Rd)
{
    if (!sf && opc_0) return UnallocatedEncoding();

    size_t datasize = sf ? 64 : 32;

    IR::U32U64 operand = X(datasize, Rn);
    IR::U32U64 result;

    if (sf) {
        result = ir.ByteReverseDual(operand);
    } else {
        result = ir.ByteReverseWord(operand);
    }
    X(datasize, Rd, result);
    return true;
}

bool TranslatorVisitor::REV32_int(Reg Rn, Reg Rd)
{
    IR::U64 operand = ir.GetX(Rn);
    IR::U32 lo = ir.ByteReverseWord(ir.LeastSignificantWord(operand));
    IR::U32 hi = ir.ByteReverseWord(ir.MostSignificantWord(operand).result);
    IR::U64 result = ir.Pack2x32To1x64(lo, hi);
    X(64, Rd, result);
    return true;
}

bool TranslatorVisitor::REV16_int(bool sf, Reg Rn, Reg Rd)
{
    size_t datasize = sf ? 64 : 32;

    IR::U32U64 operand = X(datasize, Rn);
    IR::U32U64 result;
    IR::U32U64 hihalf;
    IR::U32U64 lohalf;


    if (sf) {
        hihalf = ir.And(ir.LogicalShiftRight(IR::U64(operand), ir.Imm8(8)), ir.Imm64(0x00FF00FF00FF00FF));
        lohalf = ir.And(ir.LogicalShiftLeft(IR::U64(operand), ir.Imm8(8)), ir.Imm64(0xFF00FF00FF00FF00));
    } else {
        hihalf = ir.And(ir.LogicalShiftRight(operand, ir.Imm8(8), ir.Imm1(0)).result, ir.Imm32(0x00FF00FF));
        lohalf = ir.And(ir.LogicalShiftLeft(operand, ir.Imm8(8), ir.Imm1(0)).result, ir.Imm32(0xFF00FF00));
    }

    result = ir.Or(hihalf, lohalf);
    X(datasize, Rd, result);
    return true;
}

} // namespace A64
} // namespace Dynarmic