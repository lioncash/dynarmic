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

bool TranslatorVisitor::FMADD_float(Imm<2> type, Vec Vm, Vec Va, Vec Vn, Vec Vd) {
    const auto datasize = GetDataSize(type);
    if (!datasize || *datasize == 16) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operanda = V_scalar(*datasize, Va);
    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);
    const IR::U32U64 result = ir.FPMulAdd(operanda, operand1, operand2, true);
    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FMSUB_float(Imm<2> type, Vec Vm, Vec Va, Vec Vn, Vec Vd) {
    const auto datasize = GetDataSize(type);
    if (!datasize || *datasize == 16) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operanda = V_scalar(*datasize, Va);
    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);
    const IR::U32U64 result = ir.FPMulAdd(operanda, ir.FPNeg(operand1), operand2, true);
    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FNMADD_float(Imm<2> type, Vec Vm, Vec Va, Vec Vn, Vec Vd) {
    const auto datasize = GetDataSize(type);
    if (!datasize || *datasize == 16) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operanda = V_scalar(*datasize, Va);
    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);
    const IR::U32U64 result = ir.FPMulAdd(ir.FPNeg(operanda), ir.FPNeg(operand1), operand2, true);
    V_scalar(*datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
