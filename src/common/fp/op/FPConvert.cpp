/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <tuple>

#include "common/common_types.h"
#include "common/fp/fpcr.h"
#include "common/fp/fpsr.h"
#include "common/fp/info.h"
#include "common/fp/op/FPRecipEstimate.h"
#include "common/fp/process_exception.h"
#include "common/fp/process_nan.h"
#include "common/fp/unpacked.h"

namespace Dynarmic::FP {
namespace {
// We don't care about unreachable code warnings here
// TODO: Remove this disabling of warnings when
// half-float support is added.
#ifdef _MSC_VER
#pragma warning(disable:4702)
#endif
template <typename FPT_TO, typename FPT_FROM>
FPT_TO FPConvertNaN(FPT_FROM op) {
    const bool sign = Common::Bit<Common::BitSize<FPT_FROM>() - 1>(op);
    const u64 frac = [op] {
        if constexpr (sizeof(FPT_FROM) == sizeof(u64)) {
            return Common::Bits<0, 50>(op);
        }

        if constexpr (sizeof(FPT_FROM) == sizeof(u32)) {
            return u64{Common::Bits<0, 21>(op)} << 29;
        }

        return u64{Common::Bits<0, 8>(op)} << 42;
    }();

    const size_t dest_bit_size = Common::BitSize<FPT_TO>();
    const u64 shifted_sign = u64{sign} << (dest_bit_size - 1);
    const u64 exponent = Common::Ones<u64>(dest_bit_size - FPInfo<FPT_TO>::explicit_mantissa_width);

    if constexpr (sizeof(FPT_TO) == sizeof(u64)) {
        return FPT_TO(shifted_sign | exponent << 52 | frac);
    }

    if constexpr (sizeof(FPT_TO) == sizeof(u32)) {
        return FPT_TO(shifted_sign | exponent << 22 | Common::Bits<29, 50>(frac));
    }

    return FPT_TO(shifted_sign | exponent << 9 | Common::Bits<42, 50>(frac));
}
#ifdef _MSC_VER
#pragma warning(default:4702)
#endif
} // Anonymous namespace

template <typename FPT_TO, typename FPT_FROM>
FPT_TO FPConvert(FPT_FROM op, FPCR fpcr, RoundingMode rounding_mode, FPSR& fpsr) {
    const auto [type, sign, value] = FPUnpack<FPT_FROM>(op, fpcr, fpsr);
    const bool is_althp = Common::BitSize<FPT_TO>() == 16 && fpcr.AHP();

    if (type == FPType::SNaN || type == FPType::QNaN) {
        FPT_TO result{};

        if (is_althp) {
            result = FPInfo<FPT_TO>::Zero(sign);
        } else if (fpcr.DN()) {
            result = FPInfo<FPT_TO>::DefaultNaN();
        } else {
            result = FPConvertNaN<FPT_TO>(op);
        }

        if (type == FPType::SNaN || is_althp) {
            FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
        }

        return result;
    }

    if (type == FPType::Infinity) {
        if (is_althp) {
            FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
            return static_cast<FPT_TO>(u32{sign} << 15 | 0b111111111111111);
        }

        return FPInfo<FPT_TO>::Infinity(sign);
    }

    if (type == FPType::Zero) {
        return FPInfo<FPT_TO>::Zero(sign);
    }

    return FPRoundBase<FPT_TO>(value, fpcr, rounding_mode, fpsr);
}

template u64 FPConvert<u64, u32>(u32 op, FPCR fpcr, RoundingMode rounding_mode, FPSR& fpsr);
template u32 FPConvert<u32, u64>(u64 op, FPCR fpcr, RoundingMode rounding_mode, FPSR& fpsr);

} // namespace Dynarmic::FP
