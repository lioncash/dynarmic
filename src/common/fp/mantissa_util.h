/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/bit_util.h"
#include "common/common_types.h"

namespace Dynarmic::FP {

enum class ResidualError {
    Zero,
    LessThanHalf,
    Half,
    GreaterThanHalf,
};

template<typename MantissaT>
ResidualError ResidualErrorOnRightShift(MantissaT mantissa, int shift_amount) {
    if (shift_amount <= 0 || mantissa == 0) {
        return ResidualError::Zero;
    }

    if (shift_amount > static_cast<int>(Common::BitSize<MantissaT>())) {
        return Common::MostSignificantBit(mantissa) ? ResidualError::GreaterThanHalf : ResidualError::LessThanHalf;
    }

    const size_t half_bit_position = static_cast<size_t>(shift_amount - 1);
    const MantissaT half = static_cast<MantissaT>(1) << half_bit_position;
    const MantissaT error_mask = Common::Ones<MantissaT>(static_cast<size_t>(shift_amount));
    const MantissaT error = mantissa & error_mask;

    if (error == 0) {
        return ResidualError::Zero;
    }
    if (error < half) {
        return ResidualError::LessThanHalf;
    }
    if (error == half) {
        return ResidualError::Half;
    }
    return ResidualError::GreaterThanHalf;
}

} // namespace Dynarmic::FP 
