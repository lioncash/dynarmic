/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::EOR3(Vec Vm, Vec Va, Vec Vn, Vec Vd) {
    const IR::U128 a = ir.GetQ(Va);
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    const IR::U128 result = ir.VectorEor(ir.VectorEor(n, m), a);

    ir.SetQ(Vd, result);
    return true;
}

bool TranslatorVisitor::BCAX(Vec Vm, Vec Va, Vec Vn, Vec Vd) {
    const IR::U128 a = ir.GetQ(Va);
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    const IR::U128 result = ir.VectorEor(n, ir.VectorAnd(m, ir.VectorNot(a)));

    ir.SetQ(Vd, result);
    return true;
}

} // namespace Dynarmic::A64
