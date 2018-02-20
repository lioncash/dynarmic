/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::UCVTF_int_2(bool sz, Vec Vn, Vec Vd) {
    const auto esize = sz ? 64 : 32;

    IR::U32U64 element = V_scalar(esize, Vn);
    if (esize == 32) {
        element = ir.FPU32ToSingle(element, false, true);
    } else {
        return InterpretThisInstruction();
    }
    V_scalar(esize, Vd, element);
    return true;
}

} // namespace Dynarmic::A64
