/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::SM3TT1A(Vec Vm, Imm<2> imm2, Vec Vn, Vec Vd) {
    const IR::U128 d = ir.GetQ(Vd);
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);
    const u32 index = imm2.ZeroExtend();

    const IR::U32 top_d = ir.VectorGetElement(32, d, 3);
    const IR::U32 before_top_d = ir.VectorGetElement(32, d, 2);
    const IR::U32 after_low_d = ir.VectorGetElement(32, d, 1);
    const IR::U32 low_d = ir.VectorGetElement(32, d, 0);
    const IR::U32 top_n = ir.VectorGetElement(32, n, 3);

    const IR::U32 wj_prime = ir.VectorGetElement(32, m, index);
    const IR::U32 ss2 = ir.Eor(top_n, ir.RotateRight(top_d, ir.Imm8(20)));
    const IR::U32 tt1 = ir.Eor(after_low_d, ir.Eor(top_d, before_top_d));
    const IR::U32 final_tt1 = ir.Add(tt1, ir.Add(low_d, ir.Add(ss2, wj_prime)));

    const IR::U128 zero_vector = ir.ZeroVector();
    const IR::U128 tmp1 = ir.VectorSetElement(32, zero_vector, 0, after_low_d);
    const IR::U128 tmp2 = ir.VectorSetElement(32, tmp1, 1, ir.RotateRight(before_top_d, ir.Imm8(23)));
    const IR::U128 tmp3 = ir.VectorSetElement(32, tmp2, 2, top_d);
    const IR::U128 result = ir.VectorSetElement(32, tmp3, 3, final_tt1);

    ir.SetQ(Vd, result);
    return true;
}

} // namespace Dynarmic::A64
