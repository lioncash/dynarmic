/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::MOVN(bool sf, Imm<2> hw, Imm<16> imm16, Reg Rd) {
    size_t datasize = sf ? 64 : 32;

    if (!sf && hw.Bit<1>()) return UnallocatedEncoding();
    size_t pos = hw.ZeroExtend<size_t>() << 4;

    u64 value = imm16.ZeroExtend<u64>() << pos;
    value = ~value;
    auto result = I(datasize, value);
    X(datasize, Rd, result);

    return true;
}

bool TranslatorVisitor::MOVZ(bool sf, Imm<2> hw, Imm<16> imm16, Reg Rd) {
    size_t datasize = sf ? 64 : 32;

    if (!sf && hw.Bit<1>()) return UnallocatedEncoding();
    size_t pos = hw.ZeroExtend<size_t>() << 4;

    u64 value = imm16.ZeroExtend<u64>() << pos;
    auto result = I(datasize, value);
    X(datasize, Rd, result);

    return true;
}

bool TranslatorVisitor::MOVK(bool sf, Imm<2> hw, Imm<16> imm16, Reg Rd) {
    size_t datasize = sf ? 64 : 32;

    if (!sf && hw.Bit<1>()) return UnallocatedEncoding();
    size_t pos = hw.ZeroExtend<size_t>() << 4;

    auto result = X(datasize, Rd);
    u64 mask = u64(0xFFFF) << pos;
    u64 value = imm16.ZeroExtend<u64>() << pos;
    result = ir.And(result, I(datasize, ~mask));
    result = ir.Or(result, I(datasize, value));
    X(datasize, Rd, result);

    return true;
}


} // namespace A64
} // namespace Dynarmic
