/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::CLZ_int(bool sf, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    const IR::U32U64 operand = X(datasize, Rn);
    const IR::U32U64 result = ir.CountLeadingZeros(operand);

    X(datasize, Rd, result);
    return true;
}

bool TranslatorVisitor::REV(bool sf, bool opc_0, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    if (!sf && opc_0) return UnallocatedEncoding();

    const IR::U32U64 operand = X(datasize, Rn);

    if (sf) {
        X(datasize, Rd, ir.ByteReverseDual(operand));
    } else {
        X(datasize, Rd, ir.ByteReverseWord(operand));
    }
    return true;
}

bool TranslatorVisitor::REV32_int(Reg Rn, Reg Rd) {
    const IR::U64 operand = ir.GetX(Rn);
    const IR::U32 lo = ir.ByteReverseWord(ir.LeastSignificantWord(operand));
    const IR::U32 hi = ir.ByteReverseWord(ir.MostSignificantWord(operand).result);
    const IR::U64 result = ir.Pack2x32To1x64(lo, hi);
    X(64, Rd, result);
    return true;
}

bool TranslatorVisitor::REV16_int(bool sf, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    if (sf) {
        const IR::U64 operand = X(datasize, Rn);
        const IR::U64 hihalf = ir.And(ir.LogicalShiftRight(operand, ir.Imm8(8)), ir.Imm64(0x00FF00FF00FF00FF));
        const IR::U64 lohalf = ir.And(ir.LogicalShiftLeft(operand, ir.Imm8(8)), ir.Imm64(0xFF00FF00FF00FF00));
        const IR::U64 result = ir.Or(hihalf, lohalf);
        X(datasize, Rd, result);
    } else {
        const IR::U32 operand = X(datasize, Rn);
        const IR::U32 hihalf = ir.And(ir.LogicalShiftRight(operand, ir.Imm8(8)), ir.Imm32(0x00FF00FF));
        const IR::U32 lohalf = ir.And(ir.LogicalShiftLeft(operand, ir.Imm8(8)), ir.Imm32(0xFF00FF00));
        const IR::U32 result = ir.Or(hihalf, lohalf);
        X(datasize, Rd, result);
    }
    return true;
}

} // namespace A64
} // namespace Dynarmic
