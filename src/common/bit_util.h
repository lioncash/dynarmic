/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <bitset>
#include <climits>
#include <cstddef>
#include <type_traits>

#include "common/assert.h"

namespace Dynarmic::Common {

/// The size of a type in terms of bits
template<typename T>
constexpr size_t BitSize() {
    return sizeof(T) * CHAR_BIT;
}

template <typename T>
inline T Ones(size_t count) {
    ASSERT_MSG(count <= BitSize<T>(), "count larger than bitsize of T");
    if (count == BitSize<T>())
        return static_cast<T>(~static_cast<T>(0));
    return ~(static_cast<T>(~static_cast<T>(0)) << count);
}

/// Extract bits [begin_bit, end_bit] inclusive from value of type T.
template<size_t begin_bit, size_t end_bit, typename T>
constexpr T Bits(const T value) {
    static_assert(begin_bit <= end_bit,
                  "invalid bit range (position of beginning bit cannot be greater than that of end bit)");
    static_assert(begin_bit < BitSize<T>(), "begin_bit must be smaller than size of T");
    static_assert(end_bit < BitSize<T>(), "end_bit must be smaller than size of T");

    return (value >> begin_bit) & Ones<T>(end_bit - begin_bit + 1);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4554)
#endif
/// Extracts a single bit at bit_position from value of type T.
template<typename T>
inline bool Bit(size_t bit_position, const T value) {
    ASSERT_MSG(bit_position < BitSize<T>(), "bit_position must be smaller than size of T");

    return ((value >> bit_position) & 1) != 0;
}

/// Extracts a single bit at bit_position from value of type T.
template<size_t bit_position, typename T>
constexpr bool Bit(const T value) {
    static_assert(bit_position < BitSize<T>(), "bit_position must be smaller than size of T");

    return Bit<T>(bit_position, value);
}

/// Clears a single bit at bit_position from value of type T.
template<typename T>
inline T ClearBit(size_t bit_position, const T value) {
    ASSERT_MSG(bit_position < BitSize<T>(), "bit_position must be smaller than size of T");

    return value & ~(static_cast<T>(1) << bit_position);
}

/// Clears a single bit at bit_position from value of type T.
template<size_t bit_position, typename T>
constexpr T ClearBit(const T value) {
    static_assert(bit_position < BitSize<T>(), "bit_position must be smaller than size of T");

    return ClearBit<T>(bit_position, value);
}

/// Modifies a single bit at bit_position from value of type T.
template<typename T>
inline T ModifyBit(size_t bit_position, const T value, bool new_bit) {
    ASSERT_MSG(bit_position < BitSize<T>(), "bit_position must be smaller than size of T");

    return ClearBit<T>(bit_position, value) | (static_cast<T>(new_bit) << bit_position);
}

/// Modifies a single bit at bit_position from value of type T.
template<size_t bit_position, typename T>
constexpr T ModifyBit(const T value, bool new_bit) {
    static_assert(bit_position < BitSize<T>(), "bit_position must be smaller than size of T");

    return ModifyBit<T>(bit_position, value, new_bit);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

/// Sign-extends a value that has bit_count bits to the full bitwidth of type T.
template<size_t bit_count, typename T>
inline T SignExtend(const T value) {
    static_assert(bit_count <= BitSize<T>(), "bit_count larger than bitsize of T");

    constexpr T mask = static_cast<T>(1ULL << bit_count) - 1;
    const bool signbit = Bit<bit_count - 1, T>(value);
    if (signbit) {
        return value | ~mask;
    }
    return value;
}

/// Sign-extends a value that has bit_count bits to the full bitwidth of type T.
template<typename T>
inline T SignExtend(const size_t bit_count, const T value) {
    ASSERT_MSG(bit_count <= BitSize<T>(), "bit_count larger than bitsize of T");

    const T mask = static_cast<T>(1ULL << bit_count) - 1;
    const bool signbit = Bit<T>(bit_count - 1, value);
    if (signbit) {
        return value | ~mask;
    }
    return value;
}

template <typename Integral>
inline size_t BitCount(Integral value) {
    return std::bitset<BitSize<Integral>()>(value).count();
}

template <typename T>
inline int HighestSetBit(T value) {
    auto x = static_cast<std::make_unsigned_t<T>>(value);
    int result = -1;
    while (x != 0) {
        x >>= 1;
        result++;
    }
    return result;
}

template <typename T>
inline size_t LowestSetBit(T value) {
    auto x = static_cast<std::make_unsigned_t<T>>(value);
    if (x == 0)
        return BitSize<T>();

    size_t result = 0;
    while ((x & 1) == 0) {
        x >>= 1;
        result++;
    }
    return result;
}

template <typename T>
inline T Replicate(T value, size_t element_size) {
    ASSERT_MSG(BitSize<T>() % element_size == 0, "bitsize of T not divisible by element_size");
    if (element_size == BitSize<T>())
        return value;
    return Replicate(value | (value << element_size), element_size * 2);
}

template <typename T>
inline T RotateRight(T value, size_t amount) {
    amount %= BitSize<T>();

    if (amount == 0) {
        return value;
    }

    auto x = static_cast<std::make_unsigned_t<T>>(value);
    return static_cast<T>((x >> amount) | (x << (BitSize<T>() - amount)));
}

} // namespace Dynarmic::Common
