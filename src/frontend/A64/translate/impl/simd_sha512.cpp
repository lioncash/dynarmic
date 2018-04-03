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

} // namespace Dynarmic::A64
