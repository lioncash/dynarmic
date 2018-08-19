/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
enum class ComparisonType {
    EQ,
    GE,
    GT,
    LE,
    LT
};

enum class Signedness {
    Signed,
    Unsigned
};

bool ScalarFPCompareAgainstZero(TranslatorVisitor& v, bool sz, Vec Vn, Vec Vd, ComparisonType type) {
    const size_t esize = sz ? 64 : 32;
    const size_t datasize = esize;

    const IR::U128 operand = v.V(datasize, Vn);
    const IR::U128 zero = v.ir.ZeroVector();
    const IR::U128 result = [&] {
        switch (type) {
        case ComparisonType::EQ:
            return v.ir.FPVectorEqual(esize, operand, zero);
        case ComparisonType::GE:
            return v.ir.FPVectorGreaterEqual(esize, operand, zero);
        case ComparisonType::GT:
            return v.ir.FPVectorGreater(esize, operand, zero);
        case ComparisonType::LE:
            return v.ir.FPVectorGreaterEqual(esize, zero, operand);
        case ComparisonType::LT:
            return v.ir.FPVectorGreater(esize, zero, operand);
        }

        UNREACHABLE();
        return IR::U128{};
    }();

    v.V_scalar(datasize, Vd, v.ir.VectorGetElement(esize, result, 0));
    return true;
}

bool ScalarFPConvertWithRound(TranslatorVisitor& v, bool sz, Vec Vn, Vec Vd,
                              FP::RoundingMode rmode, Signedness sign) {
    const size_t esize = sz ? 64 : 32;

    const IR::U32U64 operand = v.V_scalar(esize, Vn);
    const IR::U32U64 result = [&]() -> IR::U32U64 {
        if (sz) {
            if (sign == Signedness::Signed) {
                return v.ir.FPDoubleToFixedS64(operand, 0, rmode);
            }

            return v.ir.FPDoubleToFixedU64(operand, 0, rmode);
        }

        if (sign == Signedness::Signed) {
            return v.ir.FPSingleToFixedS32(operand, 0, rmode);
        }

        return v.ir.FPSingleToFixedU32(operand, 0, rmode);
    }();

    v.V_scalar(esize, Vd, result);
    return true;
}

using NarrowingFn = IR::U128 (IR::IREmitter::*)(size_t, const IR::U128&);

bool SaturatedNarrow(TranslatorVisitor& v, Imm<2> size, Vec Vn, Vec Vd, NarrowingFn fn) {
    if (size == 0b11) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand = v.ir.ZeroExtendToQuad(v.V_scalar(2 * esize, Vn));
    const IR::U128 result = (v.ir.*fn)(2 * esize, operand);

    v.V_scalar(64, Vd, v.ir.VectorGetElement(64, result, 0));
    return true;
}
} // Anonymous namespace

bool TranslatorVisitor::ABS_1(Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0b11) {
        return ReservedValue();
    }

    const IR::U64 operand1 = V_scalar(64, Vn);
    const IR::U64 operand2 = ir.ArithmeticShiftRight(operand1, ir.Imm8(63));
    const IR::U64 result = ir.Sub(ir.Eor(operand1, operand2), operand2);

    V_scalar(64, Vd, result);
    return true;
}

bool TranslatorVisitor::FCMEQ_zero_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPCompareAgainstZero(*this, sz, Vn, Vd, ComparisonType::EQ);
}

bool TranslatorVisitor::FCMGE_zero_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPCompareAgainstZero(*this, sz, Vn, Vd, ComparisonType::GE);
}

bool TranslatorVisitor::FCMGT_zero_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPCompareAgainstZero(*this, sz, Vn, Vd, ComparisonType::GT);
}

bool TranslatorVisitor::FCMLE_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPCompareAgainstZero(*this, sz, Vn, Vd, ComparisonType::LE);
}

bool TranslatorVisitor::FCMLT_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPCompareAgainstZero(*this, sz, Vn, Vd, ComparisonType::LT);
}

bool TranslatorVisitor::FCVTAS_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::ToNearest_TieAwayFromZero, Signedness::Signed);
}

bool TranslatorVisitor::FCVTAU_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::ToNearest_TieAwayFromZero, Signedness::Unsigned);
}

bool TranslatorVisitor::FCVTMS_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::TowardsMinusInfinity, Signedness::Signed);
}

bool TranslatorVisitor::FCVTMU_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::TowardsMinusInfinity, Signedness::Unsigned);
}

bool TranslatorVisitor::FCVTNS_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::ToNearest_TieEven, Signedness::Signed);
}

bool TranslatorVisitor::FCVTNU_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::ToNearest_TieEven, Signedness::Unsigned);
}

bool TranslatorVisitor::FCVTPS_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::TowardsPlusInfinity, Signedness::Signed);
}

bool TranslatorVisitor::FCVTPU_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::TowardsPlusInfinity, Signedness::Unsigned);
}

bool TranslatorVisitor::FCVTZS_int_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::TowardsZero, Signedness::Signed);
}

bool TranslatorVisitor::FCVTZU_int_2(bool sz, Vec Vn, Vec Vd) {
    return ScalarFPConvertWithRound(*this, sz, Vn, Vd, FP::RoundingMode::TowardsZero, Signedness::Unsigned);
}

bool TranslatorVisitor::FRECPE_2(bool sz, Vec Vn, Vec Vd) {
    const size_t esize = sz ? 64 : 32;

    const IR::U32U64 operand = V_scalar(esize, Vn);
    const IR::U32U64 result = ir.FPRecipEstimate(operand);

    V_scalar(esize, Vd, result);
    return true;
}

bool TranslatorVisitor::FRSQRTE_2(bool sz, Vec Vn, Vec Vd) {
    const size_t esize = sz ? 64 : 32;

    const IR::U32U64 operand = V_scalar(esize, Vn);
    const IR::U32U64 result = ir.FPRSqrtEstimate(operand);

    V_scalar(esize, Vd, result);
    return true;
}

bool TranslatorVisitor::NEG_1(Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0b11) {
        return ReservedValue();
    }

    const IR::U64 operand = V_scalar(64, Vn);
    const IR::U64 result = ir.Sub(ir.Imm64(0), operand);

    V_scalar(64, Vd, result);
    return true;
}

bool TranslatorVisitor::SCVTF_int_2(bool sz, Vec Vn, Vec Vd) {
    const auto esize = sz ? 64 : 32;

    IR::U32U64 element = V_scalar(esize, Vn);
    if (esize == 32) {
        element = ir.FPS32ToSingle(element, false, true);
    } else {
        element = ir.FPS64ToDouble(element, false, true);
    }
    V_scalar(esize, Vd, element);
    return true;
}

bool TranslatorVisitor::SQXTN_1(Imm<2> size, Vec Vn, Vec Vd) {
    return SaturatedNarrow(*this, size, Vn, Vd, &IREmitter::VectorSignedSaturatedNarrowToSigned);
}

bool TranslatorVisitor::SQXTUN_1(Imm<2> size, Vec Vn, Vec Vd) {
    return SaturatedNarrow(*this, size, Vn, Vd, &IREmitter::VectorSignedSaturatedNarrowToUnsigned);
}

bool TranslatorVisitor::UCVTF_int_2(bool sz, Vec Vn, Vec Vd) {
    const auto esize = sz ? 64 : 32;

    IR::U32U64 element = V_scalar(esize, Vn);
    if (esize == 32) {
        element = ir.FPU32ToSingle(element, false, true);
    } else {
        element = ir.FPU64ToDouble(element, false, true);
    }
    V_scalar(esize, Vd, element);
    return true;
}

bool TranslatorVisitor::UQXTN_1(Imm<2> size, Vec Vn, Vec Vd) {
    return SaturatedNarrow(*this, size, Vn, Vd, &IREmitter::VectorUnsignedSaturatedNarrow);
}

} // namespace Dynarmic::A64
