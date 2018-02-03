/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <boost/optional.hpp>

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

static boost::optional<size_t> GetDataSize(Imm<2> type) {
    switch (type.ZeroExtend()) {
    case 0b00:
        return 32;
    case 0b01:
        return 64;
    case 0b11:
        return 16;
    }
    return boost::none;
}

bool TranslatorVisitor::FMOV_float_imm(Imm<2> type, Imm<8> imm8, Vec Vd) {
    boost::optional<size_t> datasize = GetDataSize(type);
    if (!datasize) {
        return UnallocatedEncoding();
    }

    IR::UAny result = [&]() -> IR::UAny {
        switch (*datasize) {
        case 16: {
            const u16 sign = imm8.Bit<7>() ? 1 : 0;
            const u16 exp = (imm8.Bit<6>() ? 0b0'1100 : 0b1'0000) | imm8.Bits<4, 5, u16>();
            const u16 fract = imm8.Bits<0, 3, u16>() << 6;
            return ir.Imm16((sign << 15) | (exp << 10) | fract);
        }
        case 32: {
            const u32 sign = imm8.Bit<7>() ? 1 : 0;
            const u32 exp = (imm8.Bit<6>() ? 0b0111'1100 : 0b1000'0000) | imm8.Bits<4, 5, u32>();
            const u32 fract = imm8.Bits<0, 3, u32>() << 19;
            return ir.Imm32((sign << 31) | (exp << 23) | fract);
        }
        case 64:
        default: {
            const u64 sign = imm8.Bit<7>() ? 1 : 0;
            const u64 exp = (imm8.Bit<6>() ? 0b011'1111'1100 : 0b100'0000'0000) | imm8.Bits<4, 5, u64>();
            const u64 fract = imm8.Bits<0, 3, u64>() << 48;
            return ir.Imm64((sign << 63) | (exp << 52) | fract);
        }
        }
    }();

    V_scalar(*datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
