/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <boost/optional.hpp>

namespace Dynarmic {
namespace Common {

/// Is 32-bit floating point value a QNaN?
constexpr bool IsQNaN(u32 value) {
    return (value & 0x7fc00000) == 0x7fc00000;
}

/// Is 32-bit floating point value a SNaN?
constexpr bool IsSNaN(u32 value) {
    return (value & 0x7fc00000) == 0x7f800000 && (value & 0x007fffff) != 0;
}

/// Is 32-bit floating point value a NaN?
constexpr bool IsNaN(u32 value) {
    return IsQNaN(value) || IsSNaN(value);
}

/// Given a pair of arguments, return the NaN value which would be returned by an ARM processor.
/// If neither argument is a NaN, returns boost::none.
inline boost::optional<u32> ProcessNaNs(u32 a, u32 b) {
    if (IsSNaN(a)) {
        return a | 0x00400000;
    } else if (IsSNaN(b)) {
        return b | 0x00400000;
    } else if (IsQNaN(a)) {
        return a;
    } else if (IsQNaN(b)) {
        return b;
    }
    return boost::none;
}

/// Given three arguments, return the NaN value which would be returned by an ARM processor.
/// If none of the arguments is a NaN, returns boost::none.
inline boost::optional<u32> ProcessNaNs(u32 a, u32 b, u32 c) {
    if (IsSNaN(a)) {
        return a | 0x00400000;
    } else if (IsSNaN(b)) {
        return b | 0x00400000;
    } else if (IsSNaN(c)) {
        return c | 0x00400000;
    } else if (IsQNaN(a)) {
        return a;
    } else if (IsQNaN(b)) {
        return b;
    } else if (IsQNaN(c)) {
        return c;
    }
    return boost::none;
}

/// Is 64-bit floating point value a QNaN?
constexpr bool IsQNaN(u64 value) {
    return (value & 0x7FF8'0000'0000'0000) == 0x7FF8'0000'0000'0000;
}

/// Is 64-bit floating point value a SNaN?
constexpr bool IsSNaN(u64 value) {
    return (value & 0x7FF8'0000'0000'0000) == 0x7FF0'0000'0000'0000
        && (value & 0x0007'FFFF'FFFF'FFFF) != 0;
}

/// Is 64-bit floating point value a NaN?
constexpr bool IsNaN(u64 value) {
    return IsQNaN(value) || IsSNaN(value);
}

/// Given a pair of arguments, return the NaN value which would be returned by an ARM processor.
/// If neither argument is a NaN, returns boost::none.
inline boost::optional<u64> ProcessNaNs(u64 a, u64 b) {
    if (IsSNaN(a)) {
        return a | 0x0008'0000'0000'0000;
    } else if (IsSNaN(b)) {
        return b | 0x0008'0000'0000'0000;
    } else if (IsQNaN(a)) {
        return a;
    } else if (IsQNaN(b)) {
        return b;
    }
    return boost::none;
}

/// Given three arguments, return the NaN value which would be returned by an ARM processor.
/// If none of the arguments is a NaN, returns boost::none.
inline boost::optional<u64> ProcessNaNs(u64 a, u64 b, u64 c) {
    if (IsSNaN(a)) {
        return a | 0x0008'0000'0000'0000;
    } else if (IsSNaN(b)) {
        return b | 0x0008'0000'0000'0000;
    } else if (IsSNaN(c)) {
        return c | 0x0008'0000'0000'0000;
    } else if (IsQNaN(a)) {
        return a;
    } else if (IsQNaN(b)) {
        return b;
    } else if (IsQNaN(c)) {
        return c;
    }
    return boost::none;
}

} // namespace Common
} // namespace Dynarmic
