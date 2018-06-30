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

bool TranslatorVisitor::FCVTZS_float_fix(bool sf, Imm<2> type, Imm<6> scale, Vec Vn, Reg Rd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }
    if (!sf && !scale.Bit<5>()) {
        return UnallocatedEncoding();
    }
    const u8 fracbits = 64 - scale.ZeroExtend<u8>();

    const IR::U32U64 fltscale = I(*fltsize, u64(fracbits + (*fltsize == 32 ? 127 : 1023)) << (*fltsize == 32 ? 23 : 52));
    const IR::U32U64 fltval = ir.FPMul(V_scalar(*fltsize, Vn), fltscale, true);

    IR::U32U64 intval;
    if (intsize == 32 && *fltsize == 32) {
        intval = ir.FPSingleToFixedS32(fltval, 0, FP::RoundingMode::TowardsZero);
    } else if (intsize == 32 && *fltsize == 64) {
        intval = ir.FPDoubleToFixedS32(fltval, 0, FP::RoundingMode::TowardsZero);
    } else if (intsize == 64 && *fltsize == 32) {
        intval = ir.FPSingleToFixedS64(fltval, 0, FP::RoundingMode::TowardsZero);
    } else if (intsize == 64 && *fltsize == 64) {
        intval = ir.FPDoubleToFixedS64(fltval, 0, FP::RoundingMode::TowardsZero);
    } else {
        UNREACHABLE();
    }

    X(intsize, Rd, intval);
    return true;
}

bool TranslatorVisitor::FCVTZU_float_fix(bool sf, Imm<2> type, Imm<6> scale, Vec Vn, Reg Rd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }
    if (!sf && !scale.Bit<5>()) {
        return UnallocatedEncoding();
    }
    const u8 fracbits = 64 - scale.ZeroExtend<u8>();

    const IR::U32U64 fltscale = I(*fltsize, u64(fracbits + (*fltsize == 32 ? 127 : 1023)) << (*fltsize == 32 ? 23 : 52));
    const IR::U32U64 fltval = ir.FPMul(V_scalar(*fltsize, Vn), fltscale, true);

    IR::U32U64 intval;
    if (intsize == 32 && *fltsize == 32) {
        intval = ir.FPSingleToFixedU32(fltval, 0, FP::RoundingMode::TowardsZero);
    } else if (intsize == 32 && *fltsize == 64) {
        intval = ir.FPDoubleToFixedU32(fltval, 0, FP::RoundingMode::TowardsZero);
    } else if (intsize == 64 && *fltsize == 32) {
        intval = ir.FPSingleToFixedU64(fltval, 0, FP::RoundingMode::TowardsZero);
    } else if (intsize == 64 && *fltsize == 64) {
        intval = ir.FPDoubleToFixedU64(fltval, 0, FP::RoundingMode::TowardsZero);
    } else {
        UNREACHABLE();
    }

    X(intsize, Rd, intval);
    return true;
}

} // namespace Dynarmic::A64
