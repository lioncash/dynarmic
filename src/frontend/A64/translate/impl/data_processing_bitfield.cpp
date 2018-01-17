/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

static IR::U32U64 ReplicateBit(IREmitter& ir, const IR::U32U64& value, u8 bit_position_to_replicate) {
    u8 datasize = value.GetType() == IR::Type::U64 ? 64 : 32;
    auto bit = ir.LogicalShiftLeft(value, ir.Imm8(datasize - 1 - bit_position_to_replicate));
    return ir.ArithmeticShiftRight(bit, ir.Imm8(datasize - 1));
}

bool TranslatorVisitor::SBFM(bool sf, bool N, Imm<6> immr, Imm<6> imms, Reg Rn, Reg Rd) {
    size_t datasize = sf ? 64 : 32;

    if (sf && !N) return ReservedValue();
    if (!sf && (N || immr.Bit<5>() || imms.Bit<5>())) return ReservedValue();

    u8 R = immr.ZeroExtend<u8>();
    u8 S = imms.ZeroExtend<u8>();
    auto masks = DecodeBitMasks(N, imms, immr, false);
    if (!masks) return ReservedValue();

    auto src = X(datasize, Rn);

    auto bot = ir.And(ir.RotateRight(src, ir.Imm8(R)), I(datasize, masks->wmask));
    auto top = ReplicateBit(ir, src, S);

    top = ir.And(top, I(datasize, ~masks->tmask));
    bot = ir.And(bot, I(datasize, masks->tmask));
    X(datasize, Rd, ir.Or(top, bot));

    return true;
}

bool TranslatorVisitor::BFM(bool sf, bool N, Imm<6> immr, Imm<6> imms, Reg Rn, Reg Rd) {
    size_t datasize = sf ? 64 : 32;

    if (sf && !N) return ReservedValue();
    if (!sf && (N || immr.Bit<5>() || imms.Bit<5>())) return ReservedValue();

    u8 R = immr.ZeroExtend<u8>();
    auto masks = DecodeBitMasks(N, imms, immr, false);
    if (!masks) return ReservedValue();

    auto dst = X(datasize, Rd);
    auto src = X(datasize, Rn);

    auto bot = ir.Or(ir.And(dst, I(datasize, ~masks->wmask)), ir.And(ir.RotateRight(src, ir.Imm8(R)), I(datasize, masks->wmask)));

    X(datasize, Rd, ir.Or(ir.And(dst, I(datasize, ~masks->tmask)), ir.And(bot, I(datasize, masks->tmask))));

    return true;
}

bool TranslatorVisitor::UBFM(bool sf, bool N, Imm<6> immr, Imm<6> imms, Reg Rn, Reg Rd) {
    size_t datasize = sf ? 64 : 32;

    if (sf && !N) return ReservedValue();
    if (!sf && (N || immr.Bit<5>() || imms.Bit<5>())) return ReservedValue();

    u8 R = immr.ZeroExtend<u8>();
    auto masks = DecodeBitMasks(N, imms, immr, false);
    if (!masks) return ReservedValue();

    auto src = X(datasize, Rn);

    auto bot = ir.And(ir.RotateRight(src, ir.Imm8(R)), I(datasize, masks->wmask));

    X(datasize, Rd, ir.And(bot, I(datasize, masks->tmask)));

    return true;
}

} // namespace A64
} // namespace Dynarmic
