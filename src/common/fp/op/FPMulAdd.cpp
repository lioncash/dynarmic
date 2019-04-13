/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/common_types.h"
#include "common/fp/fpcr.h"
#include "common/fp/fpsr.h"
#include "common/fp/info.h"
#include "common/fp/fused.h"
#include "common/fp/op/FPMulAdd.h"
#include "common/fp/process_exception.h"
#include "common/fp/process_nan.h"
#include "common/fp/unpacked.h"

namespace Dynarmic::FP {

template<typename FPT>
FPT FPMulAdd(FPT addend, FPT op1, FPT op2, FPCR fpcr, FPSR& fpsr) {
    const RoundingMode rounding = fpcr.RMode();

    const auto [typeA, signA, valueA] = FPUnpack(addend, fpcr, fpsr);
    const auto [type1, sign1, value1] = FPUnpack(op1, fpcr, fpsr);
    const auto [type2, sign2, value2] = FPUnpack(op2, fpcr, fpsr);

    const bool infA = typeA == FPType::Infinity;
    const bool inf1 = type1 == FPType::Infinity;
    const bool inf2 = type2 == FPType::Infinity;
    const bool zeroA = typeA == FPType::Zero;
    const bool zero1 = type1 == FPType::Zero;
    const bool zero2 = type2 == FPType::Zero;

    const auto maybe_nan = FPProcessNaNs3<FPT>(typeA, type1, type2, addend, op1, op2, fpcr, fpsr);

    if (typeA == FPType::QNaN && ((inf1 && zero2) || (zero1 && inf2))) {
        FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
        return FPT(FPInfo<FPT>::DefaultNaN());
    }

    if (maybe_nan) {
        return *maybe_nan;
    }

    // Calculate properties of product (op1 * op2).
    const bool signP = sign1 != sign2;
    const bool infP = inf1 || inf2;
    const bool zeroP = zero1 || zero2;

    // Raise NaN on (inf * inf) of opposite signs or (inf * zero).
    if ((inf1 && zero2) || (zero1 && inf2) || (infA && infP && signA != signP)) {
        FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
        return FPT(FPInfo<FPT>::DefaultNaN());
    }

    // Handle infinities
    if ((infA && !signA) || (infP && !signP)) {
        return FPT(FPInfo<FPT>::Infinity(false));
    }
    if ((infA && signA) || (infP && signP)) {
        return FPT(FPInfo<FPT>::Infinity(true));
    }

    // Result is exactly zero
    if (zeroA && zeroP && signA == signP) {
        return FPT(FPInfo<FPT>::Zero(signA));
    }

    const FPUnpacked result_value = FusedMulAdd(valueA, value1, value2);
    if (result_value.mantissa == 0) {
        return FPT(FPInfo<FPT>::Zero(rounding == RoundingMode::TowardsMinusInfinity));
    }
    return FPRound<FPT>(result_value, fpcr, fpsr);
}

template u16 FPMulAdd<u16>(u16 addend, u16 op1, u16 op2, FPCR fpcr, FPSR& fpsr);
template u32 FPMulAdd<u32>(u32 addend, u32 op1, u32 op2, FPCR fpcr, FPSR& fpsr);
template u64 FPMulAdd<u64>(u64 addend, u64 op1, u64 op2, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP 
