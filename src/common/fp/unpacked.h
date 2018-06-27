/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <tuple>

#include "common/common_types.h"
#include "common/fp/fpsr.h"
#include "frontend/A64/FPCR.h"

namespace Dynarmic::FP {

using FPCR = A64::FPCR;

enum class FPType {
    Nonzero,
    Zero,
    Infinity,
    QNaN,
    SNaN,
};

/// value = (sign ? -1 : +1) * mantissa * 2^exponent
template<typename MantissaT>
struct FPUnpacked {
    bool sign;
    int exponent;
    MantissaT mantissa;
};

template<typename MantissaT>
inline bool operator==(const FPUnpacked<MantissaT>& a, const FPUnpacked<MantissaT>& b) {
    return std::tie(a.sign, a.exponent, a.mantissa) == std::tie(b.sign, b.exponent, b.mantissa);
}

template<typename FPT>
std::tuple<FPType, bool, FPUnpacked<u64>> FPUnpack(FPT op, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP
