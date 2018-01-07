/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::ADD_shift(bool sf, Imm<2> shift, Reg Rm, Imm<6> imm6, Reg Rn, Reg Rd) {
    size_t datasize = sf ? 64 : 32;

    if (shift == 0b11) return ReservedValue();
    if (!sf && imm6.Bit<5>()) return ReservedValue();

    u8 shift_amount = imm6.ZeroExtend<u8>();

    auto operand1 = X(datasize, Rn);
    auto operand2 = ShiftReg(datasize, Rm, shift, ir.Imm8(shift_amount));

    auto result = ir.Add(operand1, operand2);

    X(datasize, Rd, result);

    return true;
}

} // namespace A64
} // namespace Dynarmic
