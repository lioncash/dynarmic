/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::ADDV(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if ((size == 0b10 && !Q) || size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;
    const size_t elements = datasize / esize;

    const IR::U128 operand = V(datasize, Vn);

    const auto get_element = [&](IR::U128 vec, size_t element) {
        return ir.ZeroExtendToWord(ir.VectorGetElement(esize, vec, element));
    };

    IR::U32 sum = get_element(operand, 0);
    for (size_t i = 1; i < elements; i++) {
        sum = ir.Add(sum, get_element(operand, i));
    }

    if (size == 0b00) {
        V(datasize, Vd, ir.ZeroExtendToQuad(ir.LeastSignificantByte(sum)));
    } else if (size == 0b01) {
        V(datasize, Vd, ir.ZeroExtendToQuad(ir.LeastSignificantHalf(sum)));
    } else {
        V(datasize, Vd, ir.ZeroExtendToQuad(sum));
    }

    return true;
}

bool TranslatorVisitor::UADDLV(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if ((size == 0b10 && !Q) || size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;
    const size_t elements = datasize / esize;

    const IR::U128 operand = V(datasize, Vn);

    const auto get_element = [&](IR::U128 vec, size_t element) {
        return ir.ZeroExtendToLong(ir.VectorGetElement(esize, vec, element));
    };

    IR::U64 sum = get_element(operand, 0);
    for (size_t i = 1; i < elements; i++) {
        sum = ir.Add(sum, get_element(operand, i));
    }

    if (size == 0b00) {
        V(datasize, Vd, ir.ZeroExtendToQuad(ir.LeastSignificantHalf(sum)));
    } else if (size == 0b01) {
        V(datasize, Vd, ir.ZeroExtendToQuad(ir.LeastSignificantWord(sum)));
    } else {
        V(datasize, Vd, ir.ZeroExtendToQuad(sum));
    }

    return true;
}

} // namespace Dynarmic::A64
