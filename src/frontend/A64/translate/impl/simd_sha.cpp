/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::SHA1H(Vec Vn, Vec Vd) {
    const IR::U128 data = ir.GetS(Vn);

    const IR::U128 result = ir.VectorOr(ir.VectorLogicalShiftLeft(32, data, 30),
                                        ir.VectorLogicalShiftRight(32, data, 2));

    ir.SetS(Vd, result);
    return true;
}

} // namespace Dynarmic::A64
