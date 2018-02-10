/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/bit_util.h"
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::SHL_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>() && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << Common::HighestSetBit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = concatenate(immh, immb).ZeroExtend<u8>() - static_cast<u8>(esize);

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 result = [&]{
        switch (esize) {
        case 8:
            return ir.VectorLogicalShiftLeft8(operand, shift_amount);
        case 16:
            return ir.VectorLogicalShiftLeft16(operand, shift_amount);
        case 32:
            return ir.VectorLogicalShiftLeft32(operand, shift_amount);
        case 64:
        default:
            return ir.VectorLogicalShiftLeft64(operand, shift_amount);
        }
    }();

    V(datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
