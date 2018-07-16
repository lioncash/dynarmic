/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/common_types.h"

namespace Dynarmic::FP {

template<typename FPT>
struct FPInfo {};

template<>
struct FPInfo<u32> {
    static constexpr size_t total_width = 32;
    static constexpr size_t exponent_width = 8;
    static constexpr size_t explicit_mantissa_width = 23;
    static constexpr size_t mantissa_width = explicit_mantissa_width + 1;

    static constexpr u32 implicit_leading_bit = u32(1) << explicit_mantissa_width;
    static constexpr u32 sign_mask = 0x80000000;
    static constexpr u32 exponent_mask = 0x7F800000;
    static constexpr u32 mantissa_mask = 0x007FFFFF;

    static constexpr int exponent_min = -126;
    static constexpr int exponent_max = 127;
    static constexpr int exponent_bias = 127;

    static constexpr u32 Zero(bool sign) { return sign ? sign_mask : 0; }
    static constexpr u32 Infinity(bool sign) { return exponent_mask | Zero(sign); }
    static constexpr u32 MaxNormal(bool sign) { return (exponent_mask - 1) | Zero(sign); }
    static constexpr u32 DefaultNaN() { return exponent_mask | (u32(1) << (explicit_mantissa_width - 1)); }
};

template<>
struct FPInfo<u64> {
    static constexpr size_t total_width = 64;
    static constexpr size_t exponent_width = 11;
    static constexpr size_t explicit_mantissa_width = 52;
    static constexpr size_t mantissa_width = explicit_mantissa_width + 1;

    static constexpr u64 implicit_leading_bit = u64(1) << explicit_mantissa_width;
    static constexpr u64 sign_mask = 0x8000'0000'0000'0000;
    static constexpr u64 exponent_mask = 0x7FF0'0000'0000'0000;
    static constexpr u64 mantissa_mask = 0x000F'FFFF'FFFF'FFFF;

    static constexpr int exponent_min = -1022;
    static constexpr int exponent_max = 1023;
    static constexpr int exponent_bias = 1023;

    static constexpr u64 Zero(bool sign) { return sign ? sign_mask : 0; }
    static constexpr u64 Infinity(bool sign) { return exponent_mask | Zero(sign); }
    static constexpr u64 MaxNormal(bool sign) { return (exponent_mask - 1) | Zero(sign); }
    static constexpr u64 DefaultNaN() { return exponent_mask | (u64(1) << (explicit_mantissa_width - 1)); }
};

} // namespace Dynarmic::FP 
