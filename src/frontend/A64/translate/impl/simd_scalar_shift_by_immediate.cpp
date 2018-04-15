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

bool TranslatorVisitor::SRI_1(Imm<4> immh, Imm<3> immb, Vec Vn, Vec Vd) {
    if (!immh.Bit<3>()) {
        return ReservedValue();
    }

    const size_t esize = 64;
    const u8 shift_amount = static_cast<u8>((esize * 2) - concatenate(immh, immb).ZeroExtend());
    const u64 mask = shift_amount == esize ? 0 : Common::Ones<u64>(esize) >> shift_amount;

    const IR::U64 operand1 = V_scalar(esize, Vn);
    const IR::U64 operand2 = V_scalar(esize, Vd);

    const IR::U64 shifted = ir.LogicalShiftRight(operand1, ir.Imm8(shift_amount));
    const IR::U64 result = ir.Or(ir.And(operand2, ir.Not(ir.Imm64(mask))), shifted);

    V_scalar(esize, Vd, result);
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
