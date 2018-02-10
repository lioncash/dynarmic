/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/bit_util.h"
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::MOVI(bool Q, bool op, Imm<1> a, Imm<1> b, Imm<1> c, Imm<4> cmode, Imm<1> d, Imm<1> e, Imm<1> f, Imm<1> g, Imm<1> h, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    // MOVI
    // also FMOV (vector, immediate) when cmode == 0b1111
    const auto movi = [&]{
        u64 imm64 = AdvSIMDExpandImm(op, cmode, concatenate(a, b, c, d, e, f, g, h));
        const IR::U128 imm = datasize == 64 ? ir.ZeroExtendToQuad(ir.Imm64(imm64)) : ir.VectorBroadcast(64, ir.Imm64(imm64));
        V(128, Vd, imm);
        return true;
    };

    // MVNI
    const auto mvni = [&]{
        u64 imm64 = ~AdvSIMDExpandImm(op, cmode, concatenate(a, b, c, d, e, f, g, h));
        const IR::U128 imm = datasize == 64 ? ir.ZeroExtendToQuad(ir.Imm64(imm64)) : ir.VectorBroadcast(64, ir.Imm64(imm64));
        V(128, Vd, imm);
        return true;
    };

    // ORR (vector, immediate)
    const auto orr = [&]{
        u64 imm64 = AdvSIMDExpandImm(op, cmode, concatenate(a, b, c, d, e, f, g, h));
        const IR::U128 imm = datasize == 64 ? ir.ZeroExtendToQuad(ir.Imm64(imm64)) : ir.VectorBroadcast(64, ir.Imm64(imm64));
        const IR::U128 operand = V(datasize, Vd);
        const IR::U128 result = ir.VectorOr(operand, imm);
        V(datasize, Vd, result);
        return true;
    };

    // BIC (vector, immediate)
    const auto bic = [&]{
        u64 imm64 = ~AdvSIMDExpandImm(op, cmode, concatenate(a, b, c, d, e, f, g, h));
        const IR::U128 imm = datasize == 64 ? ir.ZeroExtendToQuad(ir.Imm64(imm64)) : ir.VectorBroadcast(64, ir.Imm64(imm64));
        const IR::U128 operand = V(datasize, Vd);
        const IR::U128 result = ir.VectorAnd(operand, imm);
        V(datasize, Vd, result);
        return true;
    };

    switch (concatenate(cmode, Imm<1>{op}).ZeroExtend()) {
    case 0b00000: case 0b00100: case 0b01000: case 0b01100:
    case 0b10000: case 0b10100:
    case 0b11000: case 0b11010:
    case 0b11100: case 0b11101: case 0b11110:
        return movi();
    case 0b11111:
        if (!Q) {
            return UnallocatedEncoding();
        }
        return movi();
    case 0b00001: case 0b00101: case 0b01001: case 0b01101:
    case 0b10001: case 0b10101:
    case 0b11001: case 0b11011:
        return mvni();
    case 0b00010: case 0b00110: case 0b01010: case 0b01110:
    case 0b10010: case 0b10110:
        return orr();
    case 0b00011: case 0b00111: case 0b01011: case 0b01111:
    case 0b10011: case 0b10111:
        return bic();
    }

    UNREACHABLE();
    return true;
}

} // namespace Dynarmic::A64
