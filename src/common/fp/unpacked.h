/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <tuple>

#include "common/common_types.h"
#include "common/fp/fpcr.h"

namespace Dynarmic::FP {

class FPSR;
enum class RoundingMode;

enum class FPType {
    Nonzero,
    Zero,
    Infinity,
    QNaN,
    SNaN,
};

/// value = (sign ? -1 : +1) * mantissa * 2^exponent
struct FPUnpacked {
    bool sign;
    int exponent;
    u64 mantissa;
};

inline bool operator==(const FPUnpacked& a, const FPUnpacked& b) {
    return std::tie(a.sign, a.exponent, a.mantissa) == std::tie(b.sign, b.exponent, b.mantissa);
}

template<typename FPT>
std::tuple<FPType, bool, FPUnpacked> FPUnpack(FPT op, FPCR fpcr, FPSR& fpsr);

template<typename FPT>
FPT FPRoundBase(FPUnpacked op, FPCR fpcr, RoundingMode rounding, FPSR& fpsr);

template<typename FPT>
FPT FPRound(FPUnpacked op, FPCR fpcr, RoundingMode rounding, FPSR& fpsr) {
    fpcr.AHP(false);
    return FPRoundBase<FPT>(op, fpcr, rounding, fpsr);
}

template<typename FPT>
FPT FPRound(FPUnpacked op, FPCR fpcr, FPSR& fpsr) {
    return FPRound<FPT>(op, fpcr, fpcr.RMode(), fpsr);
}

} // namespace Dynarmic::FP
