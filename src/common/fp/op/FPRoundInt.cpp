/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/fp/fpsr.h"
#include "common/fp/info.h"
#include "common/fp/mantissa_util.h"
#include "common/fp/op/FPRoundInt.h"
#include "common/fp/process_exception.h"
#include "common/fp/process_nan.h"
#include "common/fp/rounding_mode.h"
#include "common/fp/unpacked.h"
#include "common/safe_ops.h"
#include "frontend/A64/FPCR.h"

namespace Dynarmic::FP {

template<typename FPT>
u64 FPRoundInt(FPT op, FPCR fpcr, RoundingMode rounding, bool exact, FPSR& fpsr) {
    ASSERT(rounding != RoundingMode::ToOdd);

    auto [type, sign, value] = FPUnpack<FPT>(op, fpcr, fpsr);

    if (type == FPType::SNaN || type == FPType::QNaN) {
        return FPProcessNaN(type, op, fpcr, fpsr);
    }

    if (type == FPType::Infinity) {
        return FPInfo<FPT>::Infinity(sign);
    }
    
    if (type == FPType::Zero) {
        return FPInfo<FPT>::Zero(sign);
    }

    if (value.exponent >= 0) {
        // Guaranteed to be an integer
        return op;
    }

    u64 int_result = sign ? Safe::Negate<u64>(value.mantissa) : static_cast<u64>(value.mantissa);
    const ResidualError error = ResidualErrorOnRightShift(int_result, -value.exponent);
    int_result = Safe::ArithmeticShiftLeft(int_result, value.exponent);

    bool round_up = false;
    switch (rounding) {
    case RoundingMode::ToNearest_TieEven:
        round_up = error > ResidualError::Half || (error == ResidualError::Half && Common::Bit<0>(int_result));
        break;
    case RoundingMode::TowardsPlusInfinity:
        round_up = error != ResidualError::Zero;
        break;
    case RoundingMode::TowardsMinusInfinity:
        round_up = false;
        break;
    case RoundingMode::TowardsZero:
        round_up = error != ResidualError::Zero && Common::MostSignificantBit(int_result);
        break;
    case RoundingMode::ToNearest_TieAwayFromZero:
        round_up = error > ResidualError::Half || (error == ResidualError::Half && !Common::MostSignificantBit(int_result));
        break;
    case RoundingMode::ToOdd:
        UNREACHABLE();
    }

    if (round_up) {
        int_result++;
    }

    const u64 abs_int_result = Common::MostSignificantBit(int_result) ? Safe::Negate<u64>(int_result) : static_cast<u64>(int_result);

    const FPT result = int_result == 0
                     ? FPInfo<FPT>::Zero(sign)
                     : FPRound<FPT>(FPUnpacked<u64>{sign, 0, abs_int_result}, fpcr, RoundingMode::TowardsZero, fpsr);

    if (error != ResidualError::Zero && exact) {
        FPProcessException(FPExc::Inexact, fpcr, fpsr);
    }

    return result;
}

template u64 FPRoundInt<u32>(u32 op, FPCR fpcr, RoundingMode rounding, bool exact, FPSR& fpsr);
template u64 FPRoundInt<u64>(u64 op, FPCR fpcr, RoundingMode rounding, bool exact, FPSR& fpsr);

} // namespace Dynarmic::FP 
