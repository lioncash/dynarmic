/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/fp/fpcr.h"
#include "common/fp/fpsr.h"
#include "common/fp/fused.h"
#include "common/fp/info.h"
#include "common/fp/op/FPNeg.h"
#include "common/fp/op/FPRSqrtStepFused.h"
#include "common/fp/process_exception.h"
#include "common/fp/process_nan.h"
#include "common/fp/unpacked.h"

namespace Dynarmic::FP {

template<typename FPT>
FPT FPRSqrtStepFused(FPT op1, FPT op2, FPCR fpcr, FPSR& fpsr) {
    op1 = FPNeg(op1);

    const auto [type1, sign1, value1] = FPUnpack<FPT>(op1, fpcr, fpsr);
    const auto [type2, sign2, value2] = FPUnpack<FPT>(op2, fpcr, fpsr);
    
    if (const auto maybe_nan = FPProcessNaNs(type1, type2, op1, op2, fpcr, fpsr)) {
        return *maybe_nan;
    }

    const bool inf1 = type1 == FPType::Infinity;
    const bool inf2 = type2 == FPType::Infinity;
    const bool zero1 = type1 == FPType::Zero;
    const bool zero2 = type2 == FPType::Zero;

    if ((inf1 && zero2) || (zero1 && inf2)) {
        return FPInfo<FPT>::OnePointFive(false);
    }

    if (inf1 || inf2) {
        return FPInfo<FPT>::Infinity(sign1 != sign2);
    }

    // result_value = (3.0 + (value1 * value2)) / 2.0
    FPUnpacked result_value = FusedMulAdd(ToNormalized(false, 0, 3), value1, value2);
    result_value.exponent--;

    if (result_value.mantissa == 0) {
        return FPInfo<FPT>::Zero(fpcr.RMode() == RoundingMode::TowardsMinusInfinity);
    }
    return FPRound<FPT>(result_value, fpcr, fpsr);
}

template u32 FPRSqrtStepFused<u32>(u32 op1, u32 op2, FPCR fpcr, FPSR& fpsr);
template u64 FPRSqrtStepFused<u64>(u64 op1, u64 op2, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP 
