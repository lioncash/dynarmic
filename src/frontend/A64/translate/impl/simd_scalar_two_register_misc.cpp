/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::REV16_asimd(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0) {
        return UnallocatedEncoding();
    }

    const size_t datasize = Q ? 128 : 64;
    constexpr size_t esize = 16;

    const IR::U128 data = V(datasize, Vn);
    const IR::U128 result = ir.VectorOr(ir.VectorLogicalShiftRight(esize, data, 8),
                                        ir.VectorLogicalShiftLeft(esize, data, 8));

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::REV32_asimd(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    const u32 zext_size = size.ZeroExtend();

    if (zext_size > 1) {
        return UnallocatedEncoding();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 16 << zext_size;
    const u8 shift = static_cast<u8>(8 << zext_size);

    const IR::U128 data = V(datasize, Vn);

    // TODO: Consider factoring byte swapping code out into its own opcode.
    //       Technically the rest of the following code can be a PSHUFB
    //       in the presence of SSSE3.
    IR::U128 result = ir.VectorOr(ir.VectorLogicalShiftRight(esize, data, shift),
                                  ir.VectorLogicalShiftLeft(esize, data, shift));

    // If dealing with 8-bit elements we'll need to shuffle the bytes in each halfword
    // e.g. Assume the following numbers point out bytes in a 32-bit word, we're essentially
    //      changing [3, 2, 1, 0] to [2, 3, 0, 1]
    if (zext_size == 0) {
        result = ir.VectorShuffleLowHalfwords(result, 0b10110001);
        result = ir.VectorShuffleHighHalfwords(result, 0b10110001);
    }

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::REV64_asimd(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    const u32 zext_size = size.ZeroExtend();

    if (zext_size >= 3) {
        return UnallocatedEncoding();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 16 << zext_size;
    const u8 shift = static_cast<u8>(8 << zext_size);

    const IR::U128 data = V(datasize, Vn);

    // TODO: Consider factoring byte swapping code out into its own opcode.
    //       Technically the rest of the following code can be a PSHUFB
    //       in the presence of SSSE3.
    IR::U128 result = ir.VectorOr(ir.VectorLogicalShiftRight(esize, data, shift),
                                  ir.VectorLogicalShiftLeft(esize, data, shift));

    switch (zext_size) {
    case 0: // 8-bit elements
        result = ir.VectorShuffleLowHalfwords(result, 0b00011011);
        result = ir.VectorShuffleHighHalfwords(result, 0b00011011);
        break;
    case 1: // 16-bit elements
        result = ir.VectorShuffleLowHalfwords(result, 0b01001110);
        result = ir.VectorShuffleHighHalfwords(result, 0b01001110);
        break;
    }

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::UCVTF_int_2(bool sz, Vec Vn, Vec Vd) {
    const auto esize = sz ? 64 : 32;

    IR::U32U64 element = V_scalar(esize, Vn);
    if (esize == 32) {
        element = ir.FPU32ToSingle(element, false, true);
    } else {
        return InterpretThisInstruction();
    }
    V_scalar(esize, Vd, element);
    return true;
}

} // namespace Dynarmic::A64
