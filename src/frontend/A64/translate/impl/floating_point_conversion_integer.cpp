/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <boost/optional.hpp>

#include "common/fp/rounding_mode.h"
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

bool TranslatorVisitor::SCVTF_float_int(bool sf, Imm<2> type, Reg Rn, Vec Vd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 intval = X(intsize, Rn);
    IR::U32U64 fltval;

    if (intsize == 32 && *fltsize == 32) {
        fltval = ir.FPS32ToSingle(intval, false, true);
    } else if (intsize == 32 && *fltsize == 64) {
        fltval = ir.FPS32ToDouble(intval, false, true);
    } else if (intsize == 64 && *fltsize == 32) {
        fltval = ir.FPS64ToSingle(intval, false, true);
    } else if (intsize == 64 && *fltsize == 64) {
        fltval = ir.FPS64ToDouble(intval, false, true);
    } else {
        UNREACHABLE();
    }

    V_scalar(*fltsize, Vd, fltval);

    return true;
}

bool TranslatorVisitor::UCVTF_float_int(bool sf, Imm<2> type, Reg Rn, Vec Vd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 intval = X(intsize, Rn);
    IR::U32U64 fltval;

    if (intsize == 32 && *fltsize == 32) {
        fltval = ir.FPU32ToSingle(intval, false, true);
    } else if (intsize == 32 && *fltsize == 64) {
        fltval = ir.FPU32ToDouble(intval, false, true);
    } else if (intsize == 64 && *fltsize == 32) {
        fltval = ir.FPU64ToSingle(intval, false, true);
    } else if (intsize == 64 && *fltsize == 64) {
        fltval = ir.FPU64ToDouble(intval, false, true);
    } else {
        UNREACHABLE();
    }

    V_scalar(*fltsize, Vd, fltval);

    return true;
}

bool TranslatorVisitor::FMOV_float_gen(bool sf, Imm<2> type, Imm<1> rmode_0, Imm<1> opc_0, size_t n, size_t d) {
    // NOTE:
    // opcode<2:1> == 0b11
    // rmode<1> == 0b0

    const size_t intsize = sf ? 64 : 32;
    size_t fltsize;
    switch (type.ZeroExtend()) {
    case 0b00:
        fltsize = 32;
        break;
    case 0b01:
        fltsize = 64;
        break;
    case 0b10:
        if (rmode_0 != 1) {
            return UnallocatedEncoding();
        }
        fltsize = 128;
        break;
    default:
    case 0b11:
        fltsize = 16;
        return UnallocatedEncoding();
    }

    bool integer_to_float;
    size_t part;
    switch (rmode_0.ZeroExtend()) {
    case 0b0:
        if (fltsize != 16 && fltsize != intsize) {
            return UnallocatedEncoding();
        }
        integer_to_float = opc_0 == 0b1;
        part = 0;
        break;
    default:
    case 0b1:
        if (intsize != 64 || fltsize != 128) {
            return UnallocatedEncoding();
        }
        integer_to_float = opc_0 == 0b1;
        part = 1;
        fltsize = 64;
        break;
    }

    if (integer_to_float) {
        IR::U32U64 intval = X(intsize, static_cast<Reg>(n));
        Vpart_scalar(fltsize, static_cast<Vec>(d), part, intval);
    } else {
        IR::UAny fltval = Vpart_scalar(fltsize, static_cast<Vec>(n), part);
        IR::U32U64 intval = ZeroExtend(fltval, intsize);
        X(intsize, static_cast<Reg>(d), intval);
    }

    return true;
}

static bool FloaingPointConvertSignedInteger(TranslatorVisitor& v, bool sf, Imm<2> type, Vec Vn, Reg Rd, FP::RoundingMode rounding_mode) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return v.UnallocatedEncoding();
    }

    const IR::U32U64 fltval = v.V_scalar(*fltsize, Vn);
    IR::U32U64 intval;

    if (intsize == 32 && *fltsize == 32) {
        intval = v.ir.FPSingleToFixedS32(fltval, 0, rounding_mode);
    } else if (intsize == 32 && *fltsize == 64) {
        intval = v.ir.FPDoubleToFixedS32(fltval, 0, rounding_mode);
    } else if (intsize == 64 && *fltsize == 32) {
        intval = v.ir.FPSingleToFixedS64(fltval, 0, rounding_mode);
    } else if (intsize == 64 && *fltsize == 64) {
        intval = v.ir.FPDoubleToFixedS64(fltval, 0, rounding_mode);
    } else {
        UNREACHABLE();
    }

    v.X(intsize, Rd, intval);

    return true;
}

static bool FloaingPointConvertUnsignedInteger(TranslatorVisitor& v, bool sf, Imm<2> type, Vec Vn, Reg Rd, FP::RoundingMode rounding_mode) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return v.UnallocatedEncoding();
    }

    const IR::U32U64 fltval = v.V_scalar(*fltsize, Vn);
    IR::U32U64 intval;

    if (intsize == 32 && *fltsize == 32) {
        intval = v.ir.FPSingleToFixedU32(fltval, 0, rounding_mode);
    } else if (intsize == 32 && *fltsize == 64) {
        intval = v.ir.FPDoubleToFixedU32(fltval, 0, rounding_mode);
    } else if (intsize == 64 && *fltsize == 32) {
        intval = v.ir.FPSingleToFixedU64(fltval, 0, rounding_mode);
    } else if (intsize == 64 && *fltsize == 64) {
        intval = v.ir.FPDoubleToFixedU64(fltval, 0, rounding_mode);
    } else {
        UNREACHABLE();
    }

    v.X(intsize, Rd, intval);

    return true;
}

bool TranslatorVisitor::FCVTZS_float_int(bool sf, Imm<2> type, Vec Vn, Reg Rd) {
    return FloaingPointConvertSignedInteger(*this, sf, type, Vn, Rd, FP::RoundingMode::TowardsZero);
}

bool TranslatorVisitor::FCVTZU_float_int(bool sf, Imm<2> type, Vec Vn, Reg Rd) {
    return FloaingPointConvertUnsignedInteger(*this, sf, type, Vn, Rd, FP::RoundingMode::TowardsZero);
}

} // namespace Dynarmic::A64
