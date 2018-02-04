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

bool TranslatorVisitor::FCVTZS_float_int(bool sf, Imm<2> type, Vec Vn, Reg Rd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 fltval = V_scalar(*fltsize, Vn);
    IR::U32U64 intval;

    if (intsize == 32 && *fltsize == 32) {
        intval = ir.FPSingleToS32(fltval, true, true);
    } else if (intsize == 32 && *fltsize == 64) {
        intval = ir.FPDoubleToS32(fltval, true, true);
    } else if (intsize == 64 && *fltsize == 32) {
        return InterpretThisInstruction();
    } else if (intsize == 64 && *fltsize == 64) {
        return InterpretThisInstruction();
    } else {
        UNREACHABLE();
    }

    X(intsize, Rd, intval);

    return true;
}

bool TranslatorVisitor::FCVTZU_float_int(bool sf, Imm<2> type, Vec Vn, Reg Rd) {
    const size_t intsize = sf ? 64 : 32;
    const auto fltsize = GetDataSize(type);
    if (!fltsize || *fltsize == 16) {
        return UnallocatedEncoding();
    }

    const IR::U32U64 fltval = V_scalar(*fltsize, Vn);
    IR::U32U64 intval;

    if (intsize == 32 && *fltsize == 32) {
        intval = ir.FPSingleToU32(fltval, true, true);
    } else if (intsize == 32 && *fltsize == 64) {
        intval = ir.FPDoubleToU32(fltval, true, true);
    } else if (intsize == 64 && *fltsize == 32) {
        return InterpretThisInstruction();
    } else if (intsize == 64 && *fltsize == 64) {
        return InterpretThisInstruction();
    } else {
        UNREACHABLE();
    }

    X(intsize, Rd, intval);

    return true;
}

} // namespace Dynarmic::A64
