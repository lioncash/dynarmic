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
#include "common/common_types.h"

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

/// Create a mask of type T for bits [begin_bit, end_bit] inclusive.
template<size_t begin_bit, size_t end_bit, typename T>
constexpr T Mask() {
    static_assert(begin_bit <= end_bit,
                  "invalid bit range (position of beginning bit cannot be greater than that of end bit)");
    static_assert(begin_bit < BitSize<T>(), "begin_bit must be smaller than size of T");
    static_assert(end_bit < BitSize<T>(), "end_bit must be smaller than size of T");

    return Ones<T>(end_bit - begin_bit + 1) << begin_bit;
}

/// Clears bits [begin_bit, end_bit] inclusive of value of type T.
template<size_t begin_bit, size_t end_bit, typename T>
constexpr T ClearBits(const T value) {
    return value & ~Mask<begin_bit, end_bit, T>();
}

/// Modifies bits [begin_bit, end_bit] inclusive of value of type T.
template<size_t begin_bit, size_t end_bit, typename T>
constexpr T ModifyBits(const T value, const T new_bits) {
    return ClearBits<begin_bit, end_bit, T>(value) | ((new_bits << begin_bit) & Mask<begin_bit, end_bit, T>());
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
constexpr int HighestSetBit(T value) {
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
inline bool MostSignificantBit(T value) {
    return Bit<BitSize<T>() - 1, T>(value);
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

constexpr u16 Swap16(u16 value) {
    return static_cast<u16>(u32{value} >> 8 | u32{value} << 8);
}

constexpr u32 Swap32(u32 value) {
    return ((value & 0xFF000000U) >> 24) |
           ((value & 0x00FF0000U) >>  8) |
           ((value & 0x0000FF00U) <<  8) |
           ((value & 0x000000FFU) << 24);
}

constexpr u64 Swap64(u64 value) {
    return  ((value & 0xFF00000000000000ULL) >> 56) |
            ((value & 0x00FF000000000000ULL) >> 40) |
            ((value & 0x0000FF0000000000ULL) >> 24) |
            ((value & 0x000000FF00000000ULL) >>  8) |
            ((value & 0x00000000FF000000ULL) <<  8) |
            ((value & 0x0000000000FF0000ULL) << 24) |
            ((value & 0x000000000000FF00ULL) << 40) |
            ((value & 0x00000000000000FFULL) << 56);
}

} // namespace Dynarmic::Common
