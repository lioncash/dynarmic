/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::SHA512SU0(Vec Vn, Vec Vd) {
    const IR::U128 x = ir.GetQ(Vn);
    const IR::U128 w = ir.GetQ(Vd);

    const auto make_sig = [](IREmitter& ir, IR::U64 data) {
        const IR::U64 tmp1 = ir.RotateRight(data, ir.Imm8(1));
        const IR::U64 tmp2 = ir.RotateRight(data, ir.Imm8(8));
        const IR::U64 tmp3 = ir.LogicalShiftRight(data, ir.Imm8(7));

        return ir.Eor(tmp1, ir.Eor(tmp2, tmp3));
    };

    const IR::U64 lower_x = ir.VectorGetElement(64, x, 0);
    const IR::U64 lower_w = ir.VectorGetElement(64, w, 0);
    const IR::U64 upper_w = ir.VectorGetElement(64, w, 1);

    const IR::U128 low_result = ir.ZeroExtendToQuad(ir.Add(lower_w, make_sig(ir, upper_w)));
    const IR::U64 high_result = ir.Add(upper_w, make_sig(ir, lower_x));
    const IR::U128 result = ir.VectorSetElement(64, low_result, 1, high_result);

    ir.SetQ(Vd, result);
    return true;
}

bool TranslatorVisitor::RAX1(Vec Vm, Vec Vn, Vec Vd) {
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    const IR::U128 rotated_m = ir.VectorRotateLeft(64, m, 1);
    const IR::U128 result = ir.VectorEor(n, rotated_m);

    ir.SetQ(Vd, result);
    return true;
}

bool TranslatorVisitor::SM3PARTW1(Vec Vm, Vec Vn, Vec Vd) {
    const IR::U128 d = ir.GetQ(Vd);
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    const IR::U128 eor_d_n = ir.VectorEor(d, n);

    const IR::U128 result_low_three_words = [&] {
        // Move the top-most 3 words down one element (i.e. [3, 2, 1, 0] -> [0, 3, 2, 1])
        const IR::U128 shuffled_m = ir.VectorShuffleWords(m, 0b00111001);

        // We treat the uppermost word as junk data and don't touch/use it explicitly for now.
        // Given we don't do anything with it yet, the fact we EOR into it doesn't matter.
        return ir.VectorEor(eor_d_n, ir.VectorRotateLeft(32, shuffled_m, 15));
    }();

    IR::U128 result = result_low_three_words;
    for (size_t i = 0; i < 4; i++) {
        if (i == 3) {
            const IR::U32 top_eor_d_n = ir.VectorGetElement(32, eor_d_n, 3);
            const IR::U32 low_result_word = ir.VectorGetElement(32, result, 0);
            const IR::U32 top_result_word = ir.Eor(top_eor_d_n, ir.RotateRight(low_result_word, ir.Imm8(17)));

            // Now the uppermost word is well-defined data.
            result = ir.VectorSetElement(32, result, 3, top_result_word);
        }

        const IR::U32 word = ir.VectorGetElement(32, result, i);
        const IR::U32 modified = ir.Eor(word, ir.Eor(ir.RotateRight(word, ir.Imm8(17)),
                                                     ir.RotateRight(word, ir.Imm8(9))));

        result = ir.VectorSetElement(32, result, i, modified);
    }

    ir.SetQ(Vd, result);
    return true;
}

bool TranslatorVisitor::SM3PARTW2(Vec Vm, Vec Vn, Vec Vd) {
    const IR::U128 d = ir.GetQ(Vd);
    const IR::U128 m = ir.GetQ(Vm);
    const IR::U128 n = ir.GetQ(Vn);

    const IR::U128 temp = ir.VectorEor(n, ir.VectorRotateLeft(32, m, 7));
    const IR::U128 temp_result = ir.VectorEor(d, temp);
    const IR::U32 temp2 = [&] {
        const IR::U32 rotate1 = ir.RotateRight(ir.VectorGetElement(32, temp, 0), ir.Imm8(17));
        const IR::U32 rotate2 = ir.RotateRight(rotate1, ir.Imm8(17));
        const IR::U32 rotate3 = ir.RotateRight(rotate1, ir.Imm8(9));

        return ir.Eor(rotate1, ir.Eor(rotate2, rotate3));
    }();

    const IR::U32 high_temp_result = ir.VectorGetElement(32, temp_result, 3);
    const IR::U32 replacement = ir.Eor(high_temp_result, temp2);
    const IR::U128 result = ir.VectorSetElement(32, temp_result, 3, replacement);

    ir.SetQ(Vd, result);
    return true;
}

} // namespace Dynarmic::A64
