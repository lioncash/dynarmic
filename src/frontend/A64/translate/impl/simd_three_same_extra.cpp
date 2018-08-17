/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {

using ExtensionFunction = IR::U32 (IREmitter::*)(const IR::UAny&);

bool DotProduct(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd,
                ExtensionFunction extension) {
    if (size != 0b10) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;
    const size_t elements = datasize / esize;

    const IR::U128 operand1 = v.V(datasize, Vn);
    const IR::U128 operand2 = v.V(datasize, Vm);
    IR::U128 result = v.V(datasize, Vd);

    for (size_t i = 0; i < elements; i++) {
        IR::U32 res_element = v.ir.Imm32(0);

        for (size_t j = 0; j < 4; j++) {
            const IR::U32 elem1 = (v.ir.*extension)(v.ir.VectorGetElement(8, operand1, 4 * i + j));
            const IR::U32 elem2 = (v.ir.*extension)(v.ir.VectorGetElement(8, operand2, 4 * i + j));

            res_element = v.ir.Add(res_element, v.ir.Mul(elem1, elem2));
        }

        res_element = v.ir.Add(v.ir.VectorGetElement(32, result, i), res_element);
        result = v.ir.VectorSetElement(32, result, i, res_element);
    }

    v.V(datasize, Vd, result);
    return true;
}

} // Anonymous namespace

bool TranslatorVisitor::SDOT_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    return DotProduct(*this, Q, size, Vm, Vn, Vd, &IREmitter::SignExtendToWord);
}

bool TranslatorVisitor::UDOT_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    return DotProduct(*this, Q, size, Vm, Vn, Vd, &IREmitter::ZeroExtendToWord);
}

} // namespace Dynarmic::A64
