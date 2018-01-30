/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include "common/aes.h"
#include "common/common_types.h"

namespace Dynarmic::Common {

// See section 4.2.1 in FIPS 197.
static constexpr u8 xtime(u8 x) {
    return static_cast<u8>((x << 1) ^ (((x >> 7) & 1) * 0x1B));
}

// Galois Field multiplication.
static constexpr u8 Multiply(u8 x, u8 y) {
    return static_cast<u8>(((y & 1) * x) ^
            ((y >> 1 & 1) * xtime(x)) ^
            ((y >> 2 & 1) * xtime(xtime(x))) ^
            ((y >> 3 & 1) * xtime(xtime(xtime(x)))) ^
            ((y >> 4 & 1) * xtime(xtime(xtime(xtime(x))))));
}

void MixColumns(AESState& out_state, const AESState& state) {
    for (size_t i = 0; i < out_state.size(); i += 4) {
        const u8 a = state[i];
        const u8 b = state[i + 1];
        const u8 c = state[i + 2];
        const u8 d = state[i + 3];

        const u8 tmp = a ^ b ^ c ^ d;

        out_state[i + 0] = a ^ xtime(a ^ b) ^ tmp;
        out_state[i + 1] = b ^ xtime(b ^ c) ^ tmp;
        out_state[i + 2] = c ^ xtime(c ^ d) ^ tmp;
        out_state[i + 3] = d ^ xtime(d ^ a) ^ tmp;
    }
}

void InverseMixColumns(AESState& out_state, const AESState& state) {
    for (size_t i = 0; i < out_state.size(); i += 4) {
        const u8 a = state[i];
        const u8 b = state[i + 1];
        const u8 c = state[i + 2];
        const u8 d = state[i + 3];

        out_state[i + 0] = Multiply(a, 0x0E) ^ Multiply(b, 0x0B) ^ Multiply(c, 0x0D) ^ Multiply(d, 0x09);
        out_state[i + 1] = Multiply(a, 0x09) ^ Multiply(b, 0x0E) ^ Multiply(c, 0x0B) ^ Multiply(d, 0x0D);
        out_state[i + 2] = Multiply(a, 0x0D) ^ Multiply(b, 0x09) ^ Multiply(c, 0x0E) ^ Multiply(d, 0x0B);
        out_state[i + 3] = Multiply(a, 0x0B) ^ Multiply(b, 0x0D) ^ Multiply(c, 0x09) ^ Multiply(d, 0x0E);
    }
}

} // namespace Dynarmic::Common
