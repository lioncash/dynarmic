/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::ADD_vector(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = [&]{
        switch (esize) {
        case 8:
            return ir.VectorAdd8(operand1, operand2);
        case 16:
            return ir.VectorAdd16(operand1, operand2);
        case 32:
            return ir.VectorAdd32(operand1, operand2);
        default:
            return ir.VectorAdd64(operand1, operand2);
        }
    }();

    V(datasize, Vd, result);

    return true;
}

} // namespace A64
} // namespace Dynarmic
