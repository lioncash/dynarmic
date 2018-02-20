/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::CNT(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0b00) {
        return ReservedValue();
    }
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 result = ir.VectorPopulationCount(operand);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::CMGT_zero_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 zero = ir.ZeroVector();
    const IR::U128 result = ir.VectorGreaterSigned(esize, operand, zero);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::CMEQ_zero_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 zero = ir.ZeroVector();
    IR::U128 result = ir.VectorEqual(esize, operand, zero);
    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::CMLT_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 zero = ir.ZeroVector();
    const IR::U128 result = ir.VectorLessSigned(esize, operand, zero);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::XTN(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = 64;
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand = V(2 * datasize, Vn);
    const IR::U128 result = ir.VectorNarrow(2 * esize, operand);

    Vpart(datasize, Vd, part, result);
    return true;
}

bool TranslatorVisitor::NEG_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 zero = ir.ZeroVector();
    const IR::U128 result = ir.VectorSub(esize, zero, operand);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::NOT(bool Q, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    IR::U128 result = ir.VectorNot(operand);

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

    V(datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
