/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
enum class ShiftExtraBehavior {
    None,
    Accumulate,
};

enum class Signedness {
    Signed,
    Unsigned,
};

bool ShiftRight(TranslatorVisitor& v, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                ShiftExtraBehavior behavior, Signedness signedness) {
    if (!immh.Bit<3>()) {
        return v.ReservedValue();
    }

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
    return true;
}

bool RoundingShiftRight(TranslatorVisitor& v, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                        ShiftExtraBehavior behavior, Signedness signedness) {
    if (!immh.Bit<3>()) {
        return v.ReservedValue();
    }

    const size_t esize = 64;
    const u8 shift_amount = static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend());

    const IR::U64 operand = v.V_scalar(esize, Vn);
    const IR::U64 round_bit = v.ir.LogicalShiftRight(v.ir.LogicalShiftLeft(operand, v.ir.Imm8(64 - shift_amount)), v.ir.Imm8(63));
    const IR::U64 result = [&] {
        const IR::U64 shifted = [&]() -> IR::U64 {
            if (signedness == Signedness::Signed) {
                return v.ir.ArithmeticShiftRight(operand, v.ir.Imm8(shift_amount));
            }
            return v.ir.LogicalShiftRight(operand, v.ir.Imm8(shift_amount));
        }();

        IR::U64 tmp = v.ir.Add(shifted, round_bit);

        if (behavior == ShiftExtraBehavior::Accumulate) {
            tmp = v.ir.Add(tmp, v.V_scalar(esize, Vd));
        }

        return tmp;
    }();

    v.V_scalar(esize, Vd, result);
    return true;
}

enum class ShiftDirection {
    Left,
    Right,
};

bool ShiftAndInsert(TranslatorVisitor& v, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd,
                    ShiftDirection direction) {
    if (!immh.Bit<3>()) {
        return v.ReservedValue();
    }

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
    return true;
}

bool ScalarFPConvertWithRound(TranslatorVisitor& v, Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd, Signedness sign) {
    const u32 immh_value = immh.ZeroExtend();

    if ((immh_value & 0b1110) == 0b0000) {
        return v.ReservedValue();
    }

    // TODO: We currently don't handle FP16, so bail like the ARM reference manual allows.
    if ((immh_value & 0b1110) == 0b0010) {
        return v.ReservedValue();
    }

    const size_t esize = (immh_value & 0b1000) != 0 ? 64 : 32;
    const size_t concat = concatenate(immh, immb).ZeroExtend();
    const size_t fbits = (esize * 2) - concat;

    const IR::U32U64 operand = v.V_scalar(esize, Vn);
    const IR::U32U64 result = [&]() -> IR::U32U64 {
        if (esize == 64) {
            return sign == Signedness::Signed
                   ? v.ir.FPToFixedS64(operand, fbits, FP::RoundingMode::TowardsZero)
                   : v.ir.FPToFixedU64(operand, fbits, FP::RoundingMode::TowardsZero);
        }

        return sign == Signedness::Signed
               ? v.ir.FPToFixedS32(operand, fbits, FP::RoundingMode::TowardsZero)
               : v.ir.FPToFixedU32(operand, fbits, FP::RoundingMode::TowardsZero);
    }();

    v.V_scalar(esize, Vd, result);
    return true;
}
} // Anonymous namespace

bool TranslatorVisitor::FCVTZS_fix_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, immh, immb, Vn, Vd, Signedness::Signed);
}

bool TranslatorVisitor::FCVTZU_fix_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, immh, immb, Vn, Vd, Signedness::Unsigned);
}

bool TranslatorVisitor::SLI_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftAndInsert(*this, immh, immb, Vn, Vd, ShiftDirection::Left);
}

bool TranslatorVisitor::SRI_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftAndInsert(*this, immh, immb, Vn, Vd, ShiftDirection::Right);
}

bool TranslatorVisitor::SRSHR_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return RoundingShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Signed);
}

bool TranslatorVisitor::SRSRA_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return RoundingShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Signed);
}

bool TranslatorVisitor::SSHR_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Signed);
}

bool TranslatorVisitor::SSRA_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Signed);
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

bool TranslatorVisitor::URSHR_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return RoundingShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Unsigned);
}

bool TranslatorVisitor::URSRA_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return RoundingShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Unsigned);
}

bool TranslatorVisitor::USHR_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::None, Signedness::Unsigned);
}

bool TranslatorVisitor::USRA_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    return ShiftRight(*this, immh, immb, Vn, Vd, ShiftExtraBehavior::Accumulate, Signedness::Unsigned);
}

} // namespace Dynarmic::A64
