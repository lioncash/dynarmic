/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/fp/info.h"
#include "common/fp/process_exception.h"
#include "common/fp/unpacked.h"

namespace Dynarmic::FP {

template<typename FPT>
std::tuple<FPType, bool, FPUnpacked<u64>> FPUnpack(FPT op, FPCR fpcr, FPSR& fpsr) {
    constexpr size_t sign_bit = FPInfo<FPT>::exponent_width + FPInfo<FPT>::explicit_mantissa_width;
    constexpr size_t exponent_high_bit = FPInfo<FPT>::exponent_width + FPInfo<FPT>::explicit_mantissa_width - 1;
    constexpr size_t exponent_low_bit = FPInfo<FPT>::explicit_mantissa_width;
    constexpr size_t mantissa_high_bit = FPInfo<FPT>::explicit_mantissa_width - 1;
    constexpr size_t mantissa_low_bit = 0;
    constexpr int denormal_exponent = FPInfo<FPT>::exponent_min - int(FPInfo<FPT>::explicit_mantissa_width);

    const bool sign = Common::Bit<sign_bit>(op);
    const FPT exp_raw = Common::Bits<exponent_low_bit, exponent_high_bit>(op);
    const FPT frac_raw = Common::Bits<mantissa_low_bit, mantissa_high_bit>(op);

    if (exp_raw == 0) {
        if (frac_raw == 0 || fpcr.FZ()) {
            if (frac_raw != 0) {
                FPProcessException(FPExc::InputDenorm, fpcr, fpsr);
            }
            return {FPType::Zero, sign, {sign, 0, 0}};
        }

        return {FPType::Nonzero, sign, {sign, denormal_exponent, frac_raw}};
    }

    if (exp_raw == Common::Ones<FPT>(FPInfo<FPT>::exponent_width)) {
        if (frac_raw == 0) {
            return {FPType::Infinity, sign, {sign, 1000000, 1}};
        }

        const bool is_quiet = Common::Bit<mantissa_high_bit>(frac_raw);
        return {is_quiet ? FPType::QNaN : FPType::SNaN, sign, {sign, 0, 0}};
    }

    const int exp = static_cast<int>(exp_raw) - FPInfo<FPT>::exponent_bias - FPInfo<FPT>::explicit_mantissa_width;
    const u64 frac = frac_raw | FPInfo<FPT>::implicit_leading_bit;
    return {FPType::Nonzero, sign, {sign, exp, frac}};
}

template std::tuple<FPType, bool, FPUnpacked<u64>> FPUnpack<u32>(u32 op, FPCR fpcr, FPSR& fpsr);
template std::tuple<FPType, bool, FPUnpacked<u64>> FPUnpack<u64>(u64 op, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP
