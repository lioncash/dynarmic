/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::FMUL_elt_2(bool sz, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H, Vec Vn, Vec Vd) {
    if (sz && L == 1) {
        return UnallocatedEncoding();
    }

    const size_t idxdsize = H == 1 ? 128 : 64;
    const size_t index = sz ? H.ZeroExtend() : concatenate(H, L).ZeroExtend();
    const Vec Vm = concatenate(M, Vmlo).ZeroExtend<Vec>();
    const size_t esize = sz ? 64 : 32;

    const IR::U32U64 operand = V_scalar(esize, Vn);
    const IR::U32U64 element = ir.VectorGetElement(esize, V(idxdsize, Vm), index);
    const IR::U32U64 result = ir.FPMul(operand, element, true);

    V_scalar(esize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
