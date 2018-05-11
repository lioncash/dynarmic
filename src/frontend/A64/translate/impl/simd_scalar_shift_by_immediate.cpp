/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

enum class ShiftExtraBehavior {
    None,
    Accumulate,
};

enum class Signedness {
    Signed,
    Unsigned,
};

static void ShiftRight(TranslatorVisitor& v, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                       ShiftExtraBehavior behavior, Signedness signedness) {
    const size_t esize = 64;
    const u8 shift_amount = static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend());

    const IR::U64 operand = v.V_scalar(esize, Vn);
    IR::U64 result = [&]() -> IR::U64 {
        if (signedness == Signedness::Signed) {
            return v.ir.ArithmeticShiftRight(operand, v.ir.Imm8(shift_amount));
        }
        return v.ir.LogicalShiftRight(operand, v.ir.Imm8(shift_amount));
    }();

    if (behavior == ShiftExtraBehavior::Accumulate) {
        const IR::U64 addend = v.V_scalar(esize, Vd);
        result = v.ir.Add(result, addend);
    }

    v.V_scalar(esize, Vd, result);
}

static void RoundingShiftRight(TranslatorVisitor& v, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                               ShiftExtraBehavior behavior) {
    const size_t esize = 64;
    const u8 shift_amount = static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend());

    const IR::U64 operand = v.V_scalar(esize, Vn);
    const IR::U64 round_bit = v.ir.LogicalShiftRight(v.ir.LogicalShiftLeft(operand, v.ir.Imm8(64 - shift_amount)), v.ir.Imm8(63));
    const IR::U64 result = [&] {
        IR::U64 tmp = v.ir.Add(v.ir.ArithmeticShiftRight(operand, v.ir.Imm8(shift_amount)), round_bit);

        if (behavior == ShiftExtraBehavior::Accumulate) {
            tmp = v.ir.Add(tmp, v.V_scalar(esize, Vd));
        }

        return tmp;
    }();

    v.V_scalar(esize, Vd, result);
}

enum class ShiftDirection {
    Left,
    Right,
};

static void ShiftAndInsert(TranslatorVisitor& v, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                           ShiftDirection direction) {
    const size_t esize = 64;

    const u8 shift_amount = [&] {
        if (direction == ShiftDirection::Right) {
            return static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend());
        }

        return static_cast<u8>(concatenate(immh, immb).ZeroExtend() - esize);
    }();

    const u64 mask = [&] {
        if (direction == ShiftDirection::Right) {
             return shift_amount == esize ? 0 : Common::Ones<u64>(esize) >> shift_amount;
        }

        return Common::Ones<u64>(esize) << shift_amount;
    }();

    const IR::U64 operand1 = v.V_scalar(esize, Vn);
    const IR::U64 operand2 = v.V_scalar(esize, Vd);

    const IR::U64 shifted = [&] {
        if (direction == ShiftDirection::Right) {
            return v.ir.LogicalShiftRight(operand1, v.ir.Imm8(shift_amount));
        }

        return v.ir.LogicalShiftLeft(operand1, v.ir.Imm8(shift_amount));
    }();

    const IR::U64 result = v.ir.Or(v.ir.And(operand2, v.ir.Not(v.ir.Imm64(mask))), shifted);
    v.V_scalar(esize, Vd, result);
}

bool TranslatorVisitor::SLI_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftAndInsert(*this, immh, immb, Vn, Vd, ShiftDirection::Left);
    return true;
}

bool TranslatorVisitor::SRI_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftAndInsert(*this, immh, immb, Vn, Vd, ShiftDirection::Right);
    return true;
}

bool TranslatorVisitor::SRSHR_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    RoundingShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::None);
    return true;
}

bool TranslatorVisitor::SRSRA_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    RoundingShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate);
    return true;
}

bool TranslatorVisitor::SSHR_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SSRA_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SHL_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    const size_t esize = 64;
    const u8 shift_amount = static_cast<u8>(concatenate(immh, immb).ZeroExtend() - esize);

    const IR::U64 operand = V_scalar(esize, Vn);
    const IR::U64 result = ir.LogicalShiftLeft(operand, ir.Imm8(shift_amount));

    V_scalar(esize, Vd, result);
    return true;
}

bool TranslatorVisitor::USHR_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::USRA_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Unsigned);
    return true;
}

} // namespace Dynarmic::A64
