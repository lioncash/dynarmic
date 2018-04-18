/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::MLA_elt(bool Q, Imm<2> size, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H, Vec Vn, Vec Vd) {
    const size_t idxdsize = H == 1 ? 128 : 64;

    size_t index;
    Imm<1> Vmhi{0};
    switch (size.ZeroExtend()) {
    case 0b01:
        index = concatenate(H, L, M).ZeroExtend();
        break;
    case 0b10:
        index = concatenate(H, L).ZeroExtend();
        Vmhi = M;
        break;
    default:
        return UnallocatedEncoding();
    }

    const Vec Vm = concatenate(Vmhi, Vmlo).ZeroExtend<Vec>();

    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = ir.VectorBroadcast(esize, ir.VectorGetElement(esize, V(idxdsize, Vm), index));
    const IR::U128 operand3 = V(datasize, Vd);

    const IR::U128 product = ir.VectorMultiply(esize, operand1, operand2);
    const IR::U128 result = ir.VectorAdd(esize, operand3, product);
    V(datasize, Vd, result);

    return true;
}

} // namespace Dynarmic::A64
