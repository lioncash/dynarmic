/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/bit_util.h"
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

enum class ShiftExtraBehavior {
    None,
    Accumulate,
    Round
};

enum class Signedness {
    Signed,
    Unsigned
};

static void ShiftRight(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                       ShiftExtraBehavior behavior, Signedness signedness) {
    const size_t esize = 8 << Common::HighestSetBit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = static_cast<u8>(2 * esize) - concatenate(immh, immb).ZeroExtend<u8>();

    const IR::U128 operand = v.V(datasize, Vn);
    IR::U128 result = [&] {
        if (signedness == Signedness::Signed) {
            return v.ir.VectorArithmeticShiftRight(esize, operand, shift_amount);
        }
        return v.ir.VectorLogicalShiftRight(esize, operand, shift_amount);
    }();

    if (behavior == ShiftExtraBehavior::Accumulate) {
        const IR::U128 accumulator = v.V(datasize, Vd);
        result = v.ir.VectorAdd(esize, result, accumulator);
    }

    v.V(datasize, Vd, result);
}

static void RoundingShiftRight(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                               ShiftExtraBehavior behavior, Signedness signedness) {
    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << Common::HighestSetBit(immh.ZeroExtend());
    const u8 shift_amount = static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend());
    const u64 round_value = 1ULL << (shift_amount - 1);

    const IR::U128 operand = v.V(datasize, Vn);
    const IR::U128 round_const = v.ir.VectorBroadcast(esize, v.I(esize, round_value));
    const IR::U128 round_correction = v.ir.VectorEqual(esize, v.ir.VectorAnd(operand, round_const), round_const);

    const IR::U128 result = [&] {
        if (signedness == Signedness::Signed) {
            return v.ir.VectorArithmeticShiftRight(esize, operand, shift_amount);
        }
        return v.ir.VectorLogicalShiftRight(esize, operand, shift_amount);
    }();
    IR::U128 corrected_result = v.ir.VectorSub(esize, result, round_correction);

    if (behavior == ShiftExtraBehavior::Accumulate) {
        const IR::U128 accumulator = v.V(datasize, Vd);
        corrected_result = v.ir.VectorAdd(esize, accumulator, corrected_result);
    }

    v.V(datasize, Vd, corrected_result);
}

static void ShiftRightNarrowing(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                                ShiftExtraBehavior behavior) {
    const size_t esize = 8 << Common::HighestSetBit(immh.ZeroExtend());
    const size_t source_esize = 2 * esize;
    const size_t part = Q ? 1 : 0;

    const u8 shift_amount = static_cast<u8>(source_esize - concatenate(immh, immb).ZeroExtend());

    IR::U128 operand = v.ir.GetQ(Vn);

    if (behavior == ShiftExtraBehavior::Round) {
        const u64 round_const = 1ULL << (shift_amount - 1);
        const IR::U128 round_operand = v.ir.VectorBroadcast(source_esize, v.I(source_esize, round_const));
        operand = v.ir.VectorAdd(source_esize, operand, round_operand);
    }

    const IR::U128 result = v.ir.VectorNarrow(source_esize,
                                              v.ir.VectorLogicalShiftRight(source_esize, operand, shift_amount));

    v.Vpart(64, Vd, part, result);
}

static void ShiftLeftLong(TranslatorVisitor& v, bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                          Signedness signedness) {
    const size_t esize = 8 << Common::HighestSetBit(immh.ZeroExtend());
    const size_t datasize = 64;
    const size_t part = Q ? 1 : 0;

    const u8 shift_amount = concatenate(immh, immb).ZeroExtend<u8>() - static_cast<u8>(esize);

    const IR::U128 operand = v.Vpart(datasize, Vn, part);
    const IR::U128 expanded_operand = [&] {
        if (signedness == Signedness::Signed) {
            return v.ir.VectorSignExtend(esize, operand);
        }
        return v.ir.VectorZeroExtend(esize, operand);
    }();
    const IR::U128 result = v.ir.VectorLogicalShiftLeft(2 * esize, expanded_operand, shift_amount);

    v.V(2 * datasize, Vd, result);
}

bool TranslatorVisitor::SSHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>() && !Q) {
        return ReservedValue();
    }

    ShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SRSHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    RoundingShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SRSRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    RoundingShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SSRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>() && !Q) {
        return ReservedValue();
    }

    ShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Signed);
    return true;
}

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
    const IR::U128 result = ir.VectorLogicalShiftLeft(esize, operand, shift_amount);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SHRN(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftRightNarrowing(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::None);
    return true;
}

bool TranslatorVisitor::RSHRN(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftRightNarrowing(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::Round);
    return true;
}

bool TranslatorVisitor::SSHLL(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftLeftLong(*this, Q, immh, immb, Vn, Vd, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::URSHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    RoundingShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::URSRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    RoundingShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::USHR_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>() && !Q) {
        return ReservedValue();
    }

    ShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::USRA_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>() && !Q) {
        return ReservedValue();
    }

    ShiftRight(*this, Q, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::USHLL(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }
    if (immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftLeftLong(*this, Q, immh, immb, Vn, Vd, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::SRI_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    const size_t esize = 8 << Common::HighestSetBit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend<u8>());
    const u64 mask = shift_amount == esize ? 0 : Common::Ones<u64>(esize) >> shift_amount;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vd);

    const IR::U128 shifted = ir.VectorLogicalShiftRight(esize, operand1, shift_amount);
    const IR::U128 mask_vec = ir.VectorBroadcast(esize, I(esize, mask));
    const IR::U128 result = ir.VectorOr(ir.VectorAnd(operand2, ir.VectorNot(mask_vec)), shifted);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SLI_2(bool Q, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (immh == 0b0000) {
        return DecodeError();
    }

    if (!Q && immh.Bit<3>()) {
        return ReservedValue();
    }

    const size_t esize = 8 << Common::HighestSetBit(immh.ZeroExtend());
    const size_t datasize = Q ? 128 : 64;

    const u8 shift_amount = concatenate(immh, immb).ZeroExtend<u8>() - static_cast<u8>(esize);
    const u64 mask = Common::Ones<u64>(esize) << shift_amount;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vd);

    const IR::U128 shifted = ir.VectorLogicalShiftLeft(esize, operand1, shift_amount);
    const IR::U128 mask_vec = ir.VectorBroadcast(esize, I(esize, mask));
    const IR::U128 result = ir.VectorOr(ir.VectorAnd(operand2, ir.VectorNot(mask_vec)), shifted);

    V(datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
