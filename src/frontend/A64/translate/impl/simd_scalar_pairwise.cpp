/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::ADDP_pair(Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0b11) {
        return ReservedValue();
    }

    const IR::U64 operand1 = ir.VectorGetElement(64, V(128, Vn), 0);
    const IR::U64 operand2 = ir.VectorGetElement(64, V(128, Vn), 1);
    const IR::U128 result = ir.ZeroExtendToQuad(ir.Add(operand1, operand2));
    V(128, Vd, result);
    return true;
}

bool TranslatorVisitor::FADDP_pair_2(bool size, Vec Vn, Vec Vd) {
    const size_t esize = size ? 64 : 32;

    const IR::U32U64 operand1 = ir.VectorGetElement(esize, V(128, Vn), 0);
    const IR::U32U64 operand2 = ir.VectorGetElement(esize, V(128, Vn), 1);
    const IR::U128 result = ir.ZeroExtendToQuad(ir.FPAdd(operand1, operand2, true));
    V(128, Vd, result);
    return true;
}

} // namespace A64
} // namespace Dynarmic
