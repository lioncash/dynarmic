/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::TRN1(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (!Q && size == 0b11) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 m = V(datasize, Vm);
    const IR::U128 n = V(datasize, Vn);

    const IR::U128 result = [&] {
        switch (esize) {
        case 8:
        case 16:
        case 32: {
            // Create a mask of elements we care about (e.g. for 8-bit 0x00FF00FF00FF00FF)
            const u64 mask_element = Common::Ones<u64>(esize);
            const u64 mask_value = Common::Replicate<u64>(mask_element, esize * 2);

            const IR::U128 mask = ir.VectorBroadcast(64, I(64, mask_value));
            const IR::U128 anded_m = ir.VectorAnd(m, mask);
            const IR::U128 anded_n = ir.VectorAnd(n, mask);
            return ir.VectorOr(ir.VectorLogicalShiftLeft(esize * 2, anded_m, esize), anded_n);
        }
        case 64: {
        default:
            return ir.VectorSetElement(esize, n, 1, ir.VectorGetElement(esize, m, 0));
        }
        }
    }();

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::ZIP1(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorInterleaveLower(esize, operand1, operand2);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::ZIP2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = [&]{
        if (Q) {
            return ir.VectorInterleaveUpper(esize, operand1, operand2);
        }

        // TODO: Urgh.
        const IR::U128 interleaved = ir.VectorInterleaveLower(esize, operand1, operand2);
        return ir.VectorZeroUpper(ir.VectorShuffleWords(interleaved, 0b01001110));
    }();

    V(datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
