/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::REV16_asimd(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0) {
        return UnallocatedEncoding();
    }

    const size_t datasize = Q ? 128 : 64;
    constexpr size_t esize = 16;

    const IR::U128 data = V(datasize, Vn);
    const IR::U128 result = ir.VectorOr(ir.VectorLogicalShiftRight(esize, data, 8),
                                        ir.VectorLogicalShiftLeft(esize, data, 8));

    V(datasize, Vd, result);
    return true;
}

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
