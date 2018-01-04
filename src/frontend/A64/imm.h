/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"

namespace Dynarmic {
namespace A64 {

/**
 * Imm represents an immediate value in an AArch64 instruction.
 * Imm is used during translation as a typesafe way of passing around immediates of fixed sizes.
 */
template <size_t bit_size_>
class Imm {
public:
    static constexpr size_t bit_size = bit_size_;

    explicit Imm(u32 value) : value(value) {
        ASSERT_MSG((Common::Bits<0, bit_size-1>(value) == value), "More bits in value than expected");
    }

    template <typename T = u32>
    T ZeroExtend() const {
        static_assert(Common::BitSize<T>() <= bit_size);
        return value;
    }

    template <typename T = s32>
    T SignExtend() const {
        static_assert(Common::BitSize<T>() <= bit_size);
        return static_cast<T>(Common::SignExtend<bit_size>(value));
    }

    template <size_t bit>
    bool Bit() const {
        static_assert(bit < bit_size);
        return Common::Bit<bit>(value);
    }

    bool operator==(const Imm<bit_size>& other) const {
        return value == other.value;
    }

    bool operator!=(const Imm<bit_size>& other) const {
        return value != other.value;
    }

private:
    static_assert(bit_size != 0, "Cannot have a zero-sized immediate");
    static_assert(bit_size <= 32, "Cannot have an immediate larger than the instruction size");

    u32 value;
};

template <size_t bit_size>
bool operator==(u32 a, const Imm<bit_size>& b) {
    return Imm<bit_size>{a} == b;
}

template <size_t bit_size>
bool operator==(const Imm<bit_size>& a, u32 b) {
    return Imm<bit_size>{b} == a;
}

template <size_t bit_size>
bool operator!=(u32 a, const Imm<bit_size>& b) {
    return Imm<bit_size>{a} != b;
}

template <size_t bit_size>
bool operator!=(const Imm<bit_size>& a, u32 b) {
    return Imm<bit_size>{b} != a;
}

/**
 * Concatentate immediates together.
 * Left to right correpeonds to most significant imm to least significant imm.
 * This is equivalent to a:b:...:z in ASL.
 */
template <size_t first_bit_size, size_t ...rest_bit_sizes>
auto concatenate(Imm<first_bit_size> first, Imm<rest_bit_sizes> ...rest) -> Imm<(first_bit_size + ... + rest_bit_sizes)> {
    if constexpr (sizeof...(rest) == 0) {
        return first;
    } else {
        const auto concat_rest = concatenate(rest...);
        const u32 value = (first.ZeroExtend() << concat_rest.bit_size) | concat_rest.ZeroExtend();
        return Imm<(first_bit_size + ... + rest_bit_sizes)>{value};
    }
}

} // namespace A64
} // namespace Dynarmic
