/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <tuple>
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
enum class Transposition {
    TRN1,
    TRN2,
};

bool VectorTranspose(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd,
                     Transposition type) {
    if (!Q && size == 0b11) {
        return v.ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const u8 esize = static_cast<u8>(8 << size.ZeroExtend());

    const IR::U128 m = v.V(datasize, Vm);
    const IR::U128 n = v.V(datasize, Vn);

    const IR::U128 result = [&] {
        switch (esize) {
        case 8:
        case 16:
        case 32: {
            // Create a mask of elements we care about (e.g. for 8-bit: 0x00FF00FF00FF00FF for TRN1
            // and 0xFF00FF00FF00FF00 for TRN2)
            const u64 mask_element = [&] {
                const size_t shift = type == Transposition::TRN1 ? 0 : esize;
                return Common::Ones<u64>(esize) << shift;
            }();
            const size_t doubled_esize = esize * 2;
            const u64 mask_value = Common::Replicate<u64>(mask_element, doubled_esize);

            const IR::U128 mask = v.ir.VectorBroadcast(64, v.I(64, mask_value));
            const IR::U128 anded_m = v.ir.VectorAnd(m, mask);
            const IR::U128 anded_n = v.ir.VectorAnd(n, mask);

            if (type == Transposition::TRN1) {
                return v.ir.VectorOr(v.ir.VectorLogicalShiftLeft(doubled_esize, anded_m, esize), anded_n);
            }

            return v.ir.VectorOr(v.ir.VectorLogicalShiftRight(doubled_esize, anded_n, esize), anded_m);
        }
        case 64: {
        default:
            const auto [src, src_idx, dst, dst_idx] = [type, m, n] {
                if (type == Transposition::TRN1) {
                    return std::make_tuple(m, 0, n, 1);
                }
                return std::make_tuple(n, 1, m, 0);
            }();

            return v.ir.VectorSetElement(esize, dst, dst_idx, v.ir.VectorGetElement(esize, src, src_idx));
        }
        }
    }();

    v.V(datasize, Vd, result);
    return true;
}

enum class UnzipType {
    Even,
    Odd,
};

bool VectorUnzip(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd, UnzipType type) {
    if (size == 0b11 && !Q) {
        return v.ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 n = v.V(datasize, Vn);
    const IR::U128 m = v.V(datasize, Vm);
    IR::U128 result = [&] {
        if (type == UnzipType::Even) {
            return v.ir.VectorDeinterleaveEven(esize, n, m);
        }
        return v.ir.VectorDeinterleaveOdd(esize, n, m);
    }();

    if (datasize == 64) {
        result = v.ir.VectorShuffleWords(result, 0b11011000);
    }

    v.V(datasize, Vd, result);
    return true;
}
} // Anonymous namespace

bool TranslatorVisitor::TRN1(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    return VectorTranspose(*this, Q, size, Vm, Vn, Vd, Transposition::TRN1);
}

bool TranslatorVisitor::TRN2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    return VectorTranspose(*this, Q, size, Vm, Vn, Vd, Transposition::TRN2);
}

bool TranslatorVisitor::UZP1(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    return VectorUnzip(*this, Q, size, Vm, Vn, Vd, UnzipType::Even);
}

bool TranslatorVisitor::UZP2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    return VectorUnzip(*this, Q, size, Vm, Vn, Vd, UnzipType::Odd);
}

bool TranslatorVisitor::ZIP1(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorInterleaveLower(esize, operand1, operand2);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::ZIP2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = [&]{
        if (Q) {
            return ir.VectorInterleaveUpper(esize, operand1, operand2);
        }

        // TODO: Urgh.
        const IR::U128 interleaved = ir.VectorInterleaveLower(esize, operand1, operand2);
        return ir.VectorZeroUpper(ir.VectorShuffleWords(interleaved, 0b01001110));
    }();

    V(datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
