/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <utility>
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
enum class ExtraBehavior {
    None,
    Accumulate,
    Subtract,
    MultiplyExtended,
};

bool MultiplyByElement(TranslatorVisitor& v, bool sz, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H,
                       Vec Vn, Vec Vd, ExtraBehavior extra_behavior) {
    if (sz && L == 1) {
        return v.UnallocatedEncoding();
    }

    const size_t idxdsize = H == 1 ? 128 : 64;
    const size_t index = sz ? H.ZeroExtend() : concatenate(H, L).ZeroExtend();
    const Vec Vm = concatenate(M, Vmlo).ZeroExtend<Vec>();
    const size_t esize = sz ? 64 : 32;

    const IR::U32U64 element = v.ir.VectorGetElement(esize, v.V(idxdsize, Vm), index);
    const IR::U32U64 result = [&] {
        IR::U32U64 operand1 = v.V_scalar(esize, Vn);

        if (extra_behavior == ExtraBehavior::None) {
            return v.ir.FPMul(operand1, element, true);
        }

        if (extra_behavior == ExtraBehavior::MultiplyExtended) {
            return v.ir.FPMulX(operand1, element);
        }

        if (extra_behavior == ExtraBehavior::Subtract) {
            operand1 = v.ir.FPNeg(operand1);
        }

        const IR::U32U64 operand2 = v.V_scalar(esize, Vd);
        return v.ir.FPMulAdd(operand2, operand1, element, true);
    }();

    v.V_scalar(esize, Vd, result);
    return true;
}
} // Anonymous namespace

bool TranslatorVisitor::FMLA_elt_2(bool sz, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H, Vec Vn, Vec Vd) {
    return MultiplyByElement(*this, sz, L, M, Vmlo, H, Vn, Vd, ExtraBehavior::Accumulate);
}

bool TranslatorVisitor::FMLS_elt_2(bool sz, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H, Vec Vn, Vec Vd) {
    return MultiplyByElement(*this, sz, L, M, Vmlo, H, Vn, Vd, ExtraBehavior::Subtract);
}

bool TranslatorVisitor::FMUL_elt_2(bool sz, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H, Vec Vn, Vec Vd) {
    return MultiplyByElement(*this, sz, L, M, Vmlo, H, Vn, Vd, ExtraBehavior::None);
}

bool TranslatorVisitor::FMULX_elt_2(bool sz, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H, Vec Vn, Vec Vd) {
    return MultiplyByElement(*this, sz, L, M, Vmlo, H, Vn, Vd, ExtraBehavior::MultiplyExtended);
}

bool TranslatorVisitor::SQDMULH_elt_1(Imm<2> size, Imm<1> L, Imm<1> M, Imm<4> Vmlo, Imm<1> H, Vec Vn, Vec Vd) {
    if (size == 0b00 || size == 0b11) {
        return UnallocatedEncoding();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const auto [index, Vmhi] = [=] {
        if (size == 0b01) {
            return std::make_pair(concatenate(H, L, M).ZeroExtend(), Imm<1>{0});
        }

        return std::make_pair(concatenate(H, L).ZeroExtend(), M);
    }();
    const Vec Vm = concatenate(Vmhi, Vmlo).ZeroExtend<Vec>();

    const IR::UAny operand1 = V_scalar(esize, Vn);
    const IR::UAny operand2 = ir.VectorGetElement(esize, V(128, Vm), index);
    const auto result = ir.SignedSaturatedDoublingMultiplyReturnHigh(operand1, operand2);

    ir.OrQC(result.overflow);

    V_scalar(esize, Vd, result.result);
    return true;
}

} // namespace Dynarmic::A64
