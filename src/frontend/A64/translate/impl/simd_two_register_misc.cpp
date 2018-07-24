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
    LT,
};

bool CompareAgainstZero(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vn, Vec Vd, ComparisonType type) {
    if (size == 0b11 && !Q) {
        return v.ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = v.V(datasize, Vn);
    const IR::U128 zero = v.ir.ZeroVector();
    IR::U128 result = [&] {
        switch (type) {
        case ComparisonType::EQ:
            return v.ir.VectorEqual(esize, operand, zero);
        case ComparisonType::GE:
            return v.ir.VectorGreaterEqualSigned(esize, operand, zero);
        case ComparisonType::GT:
            return v.ir.VectorGreaterSigned(esize, operand, zero);
        case ComparisonType::LE:
            return v.ir.VectorLessEqualSigned(esize, operand, zero);
        case ComparisonType::LT:
        default:
            return v.ir.VectorLessSigned(esize, operand, zero);
        }
    }();

    if (datasize == 64) {
        result = v.ir.VectorZeroUpper(result);
    }

    v.V(datasize, Vd, result);
    return true;
}

bool FPCompareAgainstZero(TranslatorVisitor& v, bool Q, bool sz, Vec Vn, Vec Vd, ComparisonType type) {
    if (sz && !Q) {
        return v.ReservedValue();
    }

    const size_t esize = sz ? 64 : 32;
    const size_t datasize = Q ? 128 : 64;

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

    v.V(datasize, Vd, result);
    return true;
}

enum class Signedness {
    Signed,
    Unsigned
};

bool IntegerConvertToFloat(TranslatorVisitor& v, bool Q, bool sz, Vec Vn, Vec Vd, Signedness signedness) {
    if (sz && !Q) {
        return v.ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = v.V(datasize, Vn);
    const IR::U128 result = [&] {
        if (signedness == Signedness::Signed) {
            return sz ? v.ir.FPVectorS64ToDouble(operand) : v.ir.FPVectorS32ToSingle(operand);
        }

        return sz ? v.ir.FPVectorU64ToDouble(operand) : v.ir.FPVectorU32ToSingle(operand);
    }();

    v.V(datasize, Vd, result);
    return true;
}
} // Anonymous namespace

bool TranslatorVisitor::CNT(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size != 0b00) {
        return ReservedValue();
    }
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 result = ir.VectorPopulationCount(operand);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::CMGE_zero_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    return CompareAgainstZero(*this, Q, size, Vn, Vd, ComparisonType::GE);
}

bool TranslatorVisitor::CMGT_zero_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    return CompareAgainstZero(*this, Q, size, Vn, Vd, ComparisonType::GT);
}

bool TranslatorVisitor::CMEQ_zero_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    return CompareAgainstZero(*this, Q, size, Vn, Vd, ComparisonType::EQ);
}

bool TranslatorVisitor::CMLE_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    return CompareAgainstZero(*this, Q, size, Vn, Vd, ComparisonType::LE);
}

bool TranslatorVisitor::CMLT_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    return CompareAgainstZero(*this, Q, size, Vn, Vd, ComparisonType::LT);
}

bool TranslatorVisitor::ABS_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (!Q && size == 0b11) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 data = V(datasize, Vn);
    const IR::U128 result = ir.VectorAbs(esize, data);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::XTN(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = 64;
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand = V(2 * datasize, Vn);
    const IR::U128 result = ir.VectorNarrow(2 * esize, operand);

    Vpart(datasize, Vd, part, result);
    return true;
}

bool TranslatorVisitor::FABS_1(bool Q, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 16;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 result = ir.FPVectorAbs(esize, operand);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FABS_2(bool Q, bool sz, Vec Vn, Vec Vd) {
    if (sz && !Q) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = sz ? 64 : 32;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 result = ir.FPVectorAbs(esize, operand);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FCMEQ_zero_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    return FPCompareAgainstZero(*this, Q, sz, Vn, Vd, ComparisonType::EQ);
}

bool TranslatorVisitor::FCMGE_zero_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    return FPCompareAgainstZero(*this, Q, sz, Vn, Vd, ComparisonType::GE);
}

bool TranslatorVisitor::FCMGT_zero_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    return FPCompareAgainstZero(*this, Q, sz, Vn, Vd, ComparisonType::GT);
}

bool TranslatorVisitor::FCMLE_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    return FPCompareAgainstZero(*this, Q, sz, Vn, Vd, ComparisonType::LE);
}

bool TranslatorVisitor::FCMLT_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    return FPCompareAgainstZero(*this, Q, sz, Vn, Vd, ComparisonType::LT);
}

bool TranslatorVisitor::FRSQRTE_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    if (sz && !Q) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = sz ? 64 : 32;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 result = ir.FPVectorRSqrtEstimate(esize, operand);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FNEG_1(bool Q, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 mask = ir.VectorBroadcast(64, I(64, 0x8000800080008000));
    const IR::U128 result = ir.VectorEor(operand, mask);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FNEG_2(bool Q, bool sz, Vec Vn, Vec Vd) {
    if (sz && !Q) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = sz ? 64 : 32;
    const size_t mask_value = esize == 64 ? 0x8000000000000000 : 0x8000000080000000;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 mask = ir.VectorBroadcast(esize, I(esize, mask_value));
    const IR::U128 result = ir.VectorEor(operand, mask);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::NEG_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    const IR::U128 zero = ir.ZeroVector();
    const IR::U128 result = ir.VectorSub(esize, zero, operand);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SQXTUN_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = 64;
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand = V(2 * datasize, Vn);
    const IR::U128 result = ir.VectorSignedSaturatedNarrowToUnsigned(2 * esize, operand);

    Vpart(datasize, Vd, part, result);
    return true;
}

bool TranslatorVisitor::SQXTN_2(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = 64;
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand = V(2 * datasize, Vn);
    const IR::U128 result = ir.VectorSignedSaturatedNarrowToSigned(2 * esize, operand);

    Vpart(datasize, Vd, part, result);
    return true;
}

bool TranslatorVisitor::NOT(bool Q, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    IR::U128 result = ir.VectorNot(operand);

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::RBIT_asimd(bool Q, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 data = V(datasize, Vn);
    const IR::U128 result = ir.VectorReverseBits(data);

    V(datasize, Vd, result);
    return true;
}

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

bool TranslatorVisitor::UADDLP(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    IR::U128 result = ir.VectorPairedAddUnsignedWiden(esize, operand);

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SADDLP(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand = V(datasize, Vn);
    IR::U128 result = ir.VectorPairedAddSignedWiden(esize, operand);

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SCVTF_int_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    return IntegerConvertToFloat(*this, Q, sz, Vn, Vd, Signedness::Signed);
}

bool TranslatorVisitor::UCVTF_int_4(bool Q, bool sz, Vec Vn, Vec Vd) {
    return IntegerConvertToFloat(*this, Q, sz, Vn, Vd, Signedness::Unsigned);
}

bool TranslatorVisitor::SHLL(bool Q, Imm<2> size, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand = ir.VectorZeroExtend(esize, Vpart(64, Vn, Q));
    const IR::U128 result = ir.VectorLogicalShiftLeft(esize * 2, operand, static_cast<u8>(esize));

    V(128, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
