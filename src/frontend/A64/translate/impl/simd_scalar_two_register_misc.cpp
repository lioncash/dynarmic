/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::ABS_1(Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0b11) {
        return ReservedValue();
    }

    const IR::U64 operand1 = V_scalar(64, Vn);
    const IR::U64 operand2 = ir.ArithmeticShiftRight(operand1, ir.Imm8(63));
    const IR::U64 result = ir.Sub(ir.Eor(operand1, operand2), operand2);

    V_scalar(64, Vd, result);
    return true;
}

bool TranslatorVisitor::NEG_1(Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0b11) {
        return ReservedValue();
    }

    const IR::U64 operand = V_scalar(64, Vn);
    const IR::U64 result = ir.Sub(ir.Imm64(0), operand);

    V_scalar(64, Vd, result);
    return true;
}

bool TranslatorVisitor::SCVTF_int_2(bool sz, Vec Vn, Vec Vd) {
    const auto esize = sz ? 64 : 32;

    IR::U32U64 element = V_scalar(esize, Vn);
    if (esize == 32) {
        element = ir.FPS32ToSingle(element, false, true);
    } else {
        element = ir.FPS64ToDouble(element, false, true);
    }
    V_scalar(esize, Vd, element);
    return true;
}

bool TranslatorVisitor::UCVTF_int_2(bool sz, Vec Vn, Vec Vd) {
    const auto esize = sz ? 64 : 32;

    IR::U32U64 element = V_scalar(esize, Vn);
    if (esize == 32) {
        element = ir.FPU32ToSingle(element, false, true);
    } else {
        element = ir.FPU64ToDouble(element, false, true);
    }
    V_scalar(esize, Vd, element);
    return true;
}

} // namespace Dynarmic::A64
