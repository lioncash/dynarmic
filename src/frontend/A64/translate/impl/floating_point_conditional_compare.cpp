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

bool TranslatorVisitor::FCCMP_float(Imm<2> type, Vec Vm, Cond cond, Vec Vn, Imm<4> nzcv) {
    const auto datasize = GetDataSize(type);
    if (!datasize || *datasize == 16) {
        return UnallocatedEncoding();
    }
    const u32 flags = nzcv.ZeroExtend<u32>() << 28;

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::NZCV then_flags = ir.FPCompare(operand1, operand2, false, true);
    const IR::NZCV else_flags = ir.NZCVFromPackedFlags(ir.Imm32(flags));
    ir.SetNZCV(ir.ConditionalSelect(cond, then_flags, else_flags));
    return true;
}

bool TranslatorVisitor::FCCMPE_float(Imm<2> type, Vec Vm, Cond cond, Vec Vn, Imm<4> nzcv) {
    const auto datasize = GetDataSize(type);
    if (!datasize || *datasize == 16) {
        return UnallocatedEncoding();
    }
    const u32 flags = nzcv.ZeroExtend<u32>() << 28;

    const IR::U32U64 operand1 = V_scalar(*datasize, Vn);
    const IR::U32U64 operand2 = V_scalar(*datasize, Vm);

    const IR::NZCV then_flags = ir.FPCompare(operand1, operand2, true, true);
    const IR::NZCV else_flags = ir.NZCVFromPackedFlags(ir.Imm32(flags));
    ir.SetNZCV(ir.ConditionalSelect(cond, then_flags, else_flags));
    return true;
}

} // namespace Dynarmic::A64
