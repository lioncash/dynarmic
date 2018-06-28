/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <cstring>
#include <type_traits>

#include "common/bit_util.h"
#include "common/common_types.h"

namespace Dynarmic {

struct u128 {
    u128() = default;
    u128(const u128&) = default;
    u128(u128&&) = default;
    u128& operator=(const u128&) = default;
    u128& operator=(u128&&) = default;

    u128(u64 lower_, u64 upper_) : lower(lower_), upper(upper_) {}

    template <typename T>
    /* implicit */ u128(T value) : lower(value), upper(0) {
        static_assert(std::is_integral_v<T>);
        static_assert(Common::BitSize<T>() <= Common::BitSize<u64>());
    }

    u64 lower = 0;
    u64 upper = 0;
};

static_assert(Common::BitSize<u128>() == 128);
static_assert(std::is_standard_layout_v<u128>);
static_assert(std::is_trivially_copyable_v<u128>);

inline u128 operator+(u128 a, u128 b) {
    u128 result;
    result.lower = a.lower + b.lower;
    result.upper = a.upper + b.upper + (a.lower > result.lower);
    return result;
}

inline u128 operator-(u128 a, u128 b) {
    u128 result;
    result.lower = a.lower - b.lower;
    result.upper = a.upper - b.upper - (a.lower < result.lower);
    return result;
}

u128 operator<<(u128 operand, int amount);
u128 operator>>(u128 operand, int amount);

} // namespace Dynarmic
