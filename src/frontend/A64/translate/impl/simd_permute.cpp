/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::ZIP1(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;

    const IR::U128 result = [&] {
        const IR::U128 operand1 = V(datasize, Vn);
        const IR::U128 operand2 = V(datasize, Vm);

        switch (size.ZeroExtend()) {
        case 0b00:
            return ir.VectorInterleaveLower8(operand1, operand2);
        case 0b01:
            return ir.VectorInterleaveLower16(operand1, operand2);
        case 0b10:
            return ir.VectorInterleaveLower32(operand1, operand2);
        case 0b11:
        default:
            return ir.VectorInterleaveLower64(operand1, operand2);
        }
    }();

    V(datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
