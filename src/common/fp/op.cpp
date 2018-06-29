/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/safe_ops.h"
#include "common/fp/fpsr.h"
#include "common/fp/mantissa_util.h"
#include "common/fp/op.h"
#include "common/fp/process_exception.h"
#include "common/fp/rounding_mode.h"
#include "common/fp/unpacked.h"
#include "frontend/A64/FPCR.h"

namespace Dynarmic::FP {

template<typename FPT>
u64 FPToFixed(size_t ibits, FPT op, size_t fbits, bool unsigned_, FPCR fpcr, RoundingMode rounding, FPSR& fpsr) {
    ASSERT(rounding != RoundingMode::ToOdd);
    ASSERT(ibits <= 64);
    ASSERT(fbits <= ibits);

    auto [type, sign, value] = FPUnpack<FPT>(op, fpcr, fpsr);

    if (type == FPType::SNaN || type == FPType::QNaN) {
        FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
    }

    // Handle zero
    if (value.mantissa == 0) {
        return 0;
    }

    if (sign && unsigned_) {
        FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
        return 0;
    }

    // value *= 2.0^fbits
    value.exponent += static_cast<int>(fbits);

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

    // Detect Overflow
    const int min_exponent_for_overflow = static_cast<int>(ibits) - static_cast<int>(Common::HighestSetBit(value.mantissa + (round_up ? 1 : 0))) - (unsigned_ ? 0 : 1);
    if (value.exponent >= min_exponent_for_overflow) {
        // Positive overflow
        if (unsigned_ || !sign) {
            FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
            return Common::Ones<u64>(ibits - (unsigned_ ? 0 : 1));
        }

        // Negative overflow
        const u64 min_value = Safe::Negate<u64>(static_cast<u64>(1) << (ibits - 1));
        if (!(value.exponent == min_exponent_for_overflow && int_result == min_value)) {
            FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
            return static_cast<u64>(1) << (ibits - 1);
        }
    }

    if (error != ResidualError::Zero) {
        FPProcessException(FPExc::Inexact, fpcr, fpsr);
    }
    return int_result & Common::Ones<u64>(ibits);
}

template u64 FPToFixed<u32>(size_t ibits, u32 op, size_t fbits, bool unsigned_, FPCR fpcr, RoundingMode rounding, FPSR& fpsr);
template u64 FPToFixed<u64>(size_t ibits, u64 op, size_t fbits, bool unsigned_, FPCR fpcr, RoundingMode rounding, FPSR& fpsr);

} // namespace Dynarmic::FP 
