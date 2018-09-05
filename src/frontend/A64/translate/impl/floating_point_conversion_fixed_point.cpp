/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <boost/optional.hpp>

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::FCVTZS_float_fix(bool sf, Imm<2> type, Imm<6> scale, Vec Vn, Reg Rd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = FPGetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }
    if (!sf && !scale.Bit<5>()) {
        return UnallocatedEncoding();
    }
    const u8 fracbits = 64 - scale.ZeroExtend<u8>();

    const IR::U32U64 fltval = V_scalar(*fltsize, Vn);
    IR::U32U64 intval;
    if (intsize == 32) {
        intval = ir.FPToFixedS32(fltval, fracbits, FP::RoundingMode::TowardsZero);
    } else if (intsize == 64) {
        intval = ir.FPToFixedS64(fltval, fracbits, FP::RoundingMode::TowardsZero);
    } else {
        UNREACHABLE();
    }

    X(intsize, Rd, intval);
    return true;
}

bool TranslatorVisitor::FCVTZU_float_fix(bool sf, Imm<2> type, Imm<6> scale, Vec Vn, Reg Rd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = FPGetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }
    if (!sf && !scale.Bit<5>()) {
        return UnallocatedEncoding();
    }
    const u8 fracbits = 64 - scale.ZeroExtend<u8>();

    const IR::U32U64 fltval = V_scalar(*fltsize, Vn);
    IR::U32U64 intval;
    if (intsize == 32) {
        intval = ir.FPToFixedU32(fltval, fracbits, FP::RoundingMode::TowardsZero);
    } else if (intsize == 64) {
        intval = ir.FPToFixedU64(fltval, fracbits, FP::RoundingMode::TowardsZero);
    } else {
        UNREACHABLE();
    }

    X(intsize, Rd, intval);
    return true;
}

} // namespace Dynarmic::A64
