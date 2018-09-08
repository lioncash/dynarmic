/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/fp/fpcr.h"
#include "common/fp/fpsr.h"
#include "common/fp/info.h"
#include "common/fp/op/FPRSqrtEstimate.h"
#include "common/fp/process_exception.h"
#include "common/fp/process_nan.h"
#include "common/fp/unpacked.h"
#include "common/math_util.h"
#include "common/safe_ops.h"

namespace Dynarmic::FP {

template<typename FPT>
FPT FPRSqrtEstimate(FPT op, FPCR fpcr, FPSR& fpsr) {
    auto [type, sign, value] = FPUnpack<FPT>(op, fpcr, fpsr);

    if (type == FPType::SNaN || type == FPType::QNaN) {
        return FPProcessNaN(type, op, fpcr, fpsr);
    }

    if (type == FPType::Zero) {
        FPProcessException(FPExc::DivideByZero, fpcr, fpsr);
        return FPInfo<FPT>::Infinity(sign);
    }

    if (sign) {
        FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
        return FPInfo<FPT>::DefaultNaN();
    }

    if (type == FPType::Infinity) {
        return FPInfo<FPT>::Zero(false);
    }

    const int result_exponent = (-(value.exponent + 1)) >> 1;
    const bool was_exponent_odd = (value.exponent) % 2 == 0;

    const u64 scaled = Safe::LogicalShiftRight(value.mantissa, normalized_point_position - (was_exponent_odd ? 7 : 8));
    const u64 estimate = Common::RecipSqrtEstimate(scaled);

    const FPT bits_exponent = static_cast<FPT>(result_exponent + FPInfo<FPT>::exponent_bias);
    const FPT bits_mantissa = static_cast<FPT>(estimate << (FPInfo<FPT>::explicit_mantissa_width - 8));
    return (bits_exponent << FPInfo<FPT>::explicit_mantissa_width) | (bits_mantissa & FPInfo<FPT>::mantissa_mask);
}

template u32 FPRSqrtEstimate<u32>(u32 op, FPCR fpcr, FPSR& fpsr);
template u64 FPRSqrtEstimate<u64>(u64 op, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP 
