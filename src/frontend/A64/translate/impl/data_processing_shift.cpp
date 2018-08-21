/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

static IR::U8 SanitizeShiftAmount(TranslatorVisitor& v, size_t datasize, const IR::U32U64& amount) {
    return v.ir.LeastSignificantByte(v.ir.And(amount, v.I(datasize, datasize - 1)));
}

bool TranslatorVisitor::LSLV(bool sf, Reg Rm, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    const IR::U32U64 operand = X(datasize, Rn);
    const IR::U32U64 shift_amount = X(datasize, Rm);

    const IR::U32U64 result = ir.LogicalShiftLeft(operand, SanitizeShiftAmount(*this, datasize, shift_amount));

    X(datasize, Rd, result);
    return true;
}

bool TranslatorVisitor::LSRV(bool sf, Reg Rm, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    const IR::U32U64 operand = X(datasize, Rn);
    const IR::U32U64 shift_amount = X(datasize, Rm);

    const IR::U32U64 result = ir.LogicalShiftRight(operand, SanitizeShiftAmount(*this, datasize, shift_amount));

    X(datasize, Rd, result);
    return true;
}

bool TranslatorVisitor::ASRV(bool sf, Reg Rm, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    const IR::U32U64 operand = X(datasize, Rn);
    const IR::U32U64 shift_amount = X(datasize, Rm);

    const IR::U32U64 result = ir.ArithmeticShiftRight(operand, SanitizeShiftAmount(*this, datasize, shift_amount));

    X(datasize, Rd, result);
    return true;
}

bool TranslatorVisitor::RORV(bool sf, Reg Rm, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    const IR::U32U64 operand = X(datasize, Rn);
    const IR::U32U64 shift_amount = X(datasize, Rm);

    const IR::U32U64 result = ir.RotateRight(operand, SanitizeShiftAmount(*this, datasize, shift_amount));

    X(datasize, Rd, result);
    return true;
}

} // namespace Dynarmic::A64
