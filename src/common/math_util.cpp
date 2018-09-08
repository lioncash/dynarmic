/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include "common/math_util.h"

namespace Dynarmic::Common {

u8 RecipEstimate(u64 a) {
    using LUT = std::array<u8, 256>;
    static constexpr u64 lut_offset = 256;

    static const LUT lut = [] {
        LUT result{};
        for (u64 i = 0; i < result.size(); i++) {
            u64 a = i + lut_offset;

            a = a * 2 + 1;
            u64 b = (1u << 19) / a;
            result[i] = static_cast<u8>((b + 1) / 2);
        }
        return result;
    }();

    return lut[a - lut_offset];
}

} // namespace Dynarmic::Common
