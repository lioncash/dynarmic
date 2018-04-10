/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::RAX1(Vec Vm, Vec Vn, Vec Vd) {
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    const IR::U128 rotated_m = ir.VectorOr(ir.VectorLogicalShiftLeft(64, m, 1),
                                           ir.VectorLogicalShiftRight(64, m, 63));
    const IR::U128 result = ir.VectorEor(n, rotated_m);

    ir.SetQ(Vd, result);
    return true;
}

bool TranslatorVisitor::SM3PARTW2(Vec Vm, Vec Vn, Vec Vd) {
    const IR::U128 d = ir.GetQ(Vd);
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    const IR::U128 temp = ir.VectorEor(n, ir.VectorRotateLeft(32, m, 7));
    const IR::U128 temp_result = ir.VectorEor(d, temp);
    const IR::U32 temp2 = [&] {
        const IR::U32 rotate1 = ir.RotateRight(ir.VectorGetElement(32, temp, 0), ir.Imm8(17));
        const IR::U32 rotate2 = ir.RotateRight(rotate1, ir.Imm8(17));
        const IR::U32 rotate3 = ir.RotateRight(rotate1, ir.Imm8(9));

        return ir.Eor(rotate1, ir.Eor(rotate2, rotate3));
    }();

    const IR::U32 high_temp_result = ir.VectorGetElement(32, temp_result, 3);
    const IR::U32 replacement = ir.Eor(high_temp_result, temp2);
    const IR::U128 result = ir.VectorSetElement(32, temp_result, 3, replacement);

    ir.SetQ(Vd, result);
    return true;
}

} // namespace Dynarmic::A64
