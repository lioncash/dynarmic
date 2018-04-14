/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::SHA1SU0(Vec Vm, Vec Vn, Vec Vd) {
    const IR::U128 d = ir.GetQ(Vd);
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    IR::U128 result = [&] {
        const IR::U64 d_high = ir.VectorGetElement(64, d, 1);
        const IR::U64 n_low = ir.VectorGetElement(64, n, 0);
        const IR::U128 zero = ir.ZeroVector();

        const IR::U128 tmp1 = ir.VectorSetElement(64, zero, 0, d_high);
        return ir.VectorSetElement(64, tmp1, 1, n_low);
    }();

    result = ir.VectorEor(ir.VectorEor(result, d), m);

    ir.SetQ(Vd, result);
    return true;
}

bool TranslatorVisitor::SHA1SU1(Vec Vn, Vec Vd) {
    const IR::U128 d = ir.GetQ(Vd);
    const IR::U128 n = ir.GetQ(Vn);

    // Shuffle down the whole vector and zero out the top 32 bits
    const IR::U128 shuffled_n = ir.VectorSetElement(32, ir.VectorShuffleWords(n, 0b00111001), 3, ir.Imm32(0));
    const IR::U128 t = ir.VectorEor(d, shuffled_n);
    const IR::U128 rotated_t = ir.VectorRotateLeft(32, t, 1);

    const IR::U32 low_rotated_t = ir.RotateRight(ir.VectorGetElement(32, rotated_t, 0), ir.Imm8(31));
    const IR::U32 high_t = ir.VectorGetElement(32, rotated_t, 3);
    const IR::U128 result = ir.VectorSetElement(32, rotated_t, 3, ir.Eor(low_rotated_t, high_t));

    ir.SetQ(Vd, result);
    return true;
}

bool TranslatorVisitor::SHA1H(Vec Vn, Vec Vd) {
    const IR::U128 data = ir.GetS(Vn);

    const IR::U128 result = ir.VectorOr(ir.VectorLogicalShiftLeft(32, data, 30),
                                        ir.VectorLogicalShiftRight(32, data, 2));

    ir.SetS(Vd, result);
    return true;
}

} // namespace Dynarmic::A64
