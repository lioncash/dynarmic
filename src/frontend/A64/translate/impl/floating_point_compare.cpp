/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <boost/optional.hpp>

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

static boost::optional<size_t> GetDataSize(Imm<2> type) {
    switch (type.ZeroExtend()) {
    case 0b00:
        return 32;
    case 0b01:
        return 64;
    case 0b11:
        // FP16Ext, unimplemented.
        return boost::none;
    }
    return boost::none;
}

bool TranslatorVisitor::FCMP_float(Imm<2> type, Vec Vm, Vec Vn, bool cmp_with_zero) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    IR::U32U64 operand2;
    if (cmp_with_zero) {
        operand2 = I(*datasize, 0);
    } else {
        operand2 = V_scalar(*datasize, Vm);
    }

    auto nzcv = ir.FPCompare(operand1, operand2, false, true);
    ir.SetNZCV(nzcv);
    return true;
}

bool TranslatorVisitor::FCMPE_float(Imm<2> type, Vec Vm, Vec Vn, bool cmp_with_zero) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    IR::U32U64 operand2;
    if (cmp_with_zero) {
        operand2 = I(*datasize, 0);
    } else {
        operand2 = V_scalar(*datasize, Vm);
    }

    auto nzcv = ir.FPCompare(operand1, operand2, true, true);
    ir.SetNZCV(nzcv);
    return true;
}

} // namespace Dynarmic::A64
