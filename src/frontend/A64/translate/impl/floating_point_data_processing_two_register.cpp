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

bool TranslatorVisitor::FMUL_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPMul(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FDIV_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPDiv(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FADD_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPAdd(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FSUB_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPSub(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FMAX_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPMax(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FMIN_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPMin(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FMAXNM_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPMaxNumeric(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FMINNM_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPMinNumeric(operand1, operand2, true);

    V_scalar(*datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FNMUL_float(Imm<2> type, Vec Vm, Vec Vn, Vec Vd) {
    auto datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::U32U64 result = ir.FPNeg(ir.FPMul(operand1, operand2, true));

    V_scalar(*datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
