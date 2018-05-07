/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
enum class HighNarrowingOp {
    Add,
    Subtract,
};

enum class ExtraBehavior {
    None,
    Round
};

static void HighNarrowingOperation(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd,
                                   HighNarrowingOp op, ExtraBehavior behavior) {
    const size_t part = Q;
    const size_t esize = 8 << size.ZeroExtend();
    const size_t doubled_esize = 2 * esize;

    const IR::U128 operand1 = v.ir.GetQ(Vn);
    const IR::U128 operand2 = v.ir.GetQ(Vm);
    IR::U128 wide = [&] {
        if (op == HighNarrowingOp::Add) {
            return v.ir.VectorAdd(doubled_esize, operand1, operand2);
        }
        return v.ir.VectorSub(doubled_esize, operand1, operand2);
    }();

    if (behavior == ExtraBehavior::Round) {
        const u64 round_const = 1ULL << (esize - 1);
        const IR::U128 round_operand = v.ir.VectorBroadcast(doubled_esize, v.I(doubled_esize, round_const));
        wide = v.ir.VectorAdd(doubled_esize, wide, round_operand);
    }

    const IR::U128 result = v.ir.VectorNarrow(doubled_esize,
                                              v.ir.VectorLogicalShiftRight(doubled_esize, wide, static_cast<u8>(esize)));

    v.Vpart(64, Vd, part, result);
}
} // Anonymous namespace

bool TranslatorVisitor::CMGT_reg_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorGreaterSigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::CMGE_reg_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    IR::U128 result = ir.VectorGreaterEqualSigned(esize, operand1, operand2);
    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SMAX(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorMaxSigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SMIN(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorMinSigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::ADD_vector(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = ir.VectorAdd(esize, operand1, operand2);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::MLA_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 operand3 = V(datasize, Vd);

    const IR::U128 result = ir.VectorAdd(esize, ir.VectorMultiply(esize, operand1, operand2), operand3);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::MUL_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);

    const IR::U128 result = ir.VectorMultiply(esize, operand1, operand2);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::ADDHN(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    HighNarrowingOperation(*this, Q, size, Vm, Vn, Vd, HighNarrowingOp::Add, ExtraBehavior::None);
    return true;
}

bool TranslatorVisitor::RADDHN(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    HighNarrowingOperation(*this, Q, size, Vm, Vn, Vd, HighNarrowingOp::Add, ExtraBehavior::Round);
    return true;
}

bool TranslatorVisitor::SUBHN(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    HighNarrowingOperation(*this, Q, size, Vm, Vn, Vd, HighNarrowingOp::Subtract, ExtraBehavior::None);
    return true;
}

bool TranslatorVisitor::RSUBHN(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    HighNarrowingOperation(*this, Q, size, Vm, Vn, Vd, HighNarrowingOp::Subtract, ExtraBehavior::Round);
    return true;
}

bool TranslatorVisitor::SHADD(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorHalvingAddSigned(esize, operand1, operand2);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SHSUB(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorHalvingSubSigned(esize, operand1, operand2);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::UHADD(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorHalvingAddUnsigned(esize, operand1, operand2);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::ADDP_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);

    const IR::U128 result = Q ? ir.VectorPairedAdd(esize, operand1, operand2) : ir.VectorPairedAddLower(esize, operand1, operand2);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::FADD_2(bool Q, bool sz, Vec Vm, Vec Vn, Vec Vd) {
    if (sz && !Q) {
        return ReservedValue();
    }
    const size_t esize = sz ? 64 : 32;
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.FPVectorAdd(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::AND_asimd(bool Q, Vec Vm, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = ir.VectorAnd(operand1, operand2);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::BIC_asimd_reg(bool Q, Vec Vm, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);

    IR::U128 result = ir.VectorAnd(operand1, ir.VectorNot(operand2));

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::CMHI_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorGreaterUnsigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::CMHS_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    IR::U128 result = ir.VectorGreaterEqualUnsigned(esize, operand1, operand2);
    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::CMTST_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 anded = ir.VectorAnd(operand1, operand2);
    const IR::U128 result = ir.VectorNot(ir.VectorEqual(esize, anded, ir.ZeroVector()));

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::SSHL_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorLogicalVShiftSigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::USHL_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorLogicalVShiftUnsigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::UMAX(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorMaxUnsigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::UABA(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 initial_dest = V(datasize, Vd);

    const IR::U128 result = ir.VectorAdd(esize, initial_dest,
                                         ir.VectorUnsignedAbsoluteDifference(esize, operand1, operand2));

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::UABD(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorUnsignedAbsoluteDifference(esize, operand1, operand2);

    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::UMIN(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.VectorMinUnsigned(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FSUB_2(bool Q, bool sz, Vec Vm, Vec Vn, Vec Vd) {
    if (sz && !Q) {
        return ReservedValue();
    }
    const size_t esize = sz ? 64 : 32;
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 result = ir.FPVectorSub(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::ORR_asimd_reg(bool Q, Vec Vm, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = ir.VectorOr(operand1, operand2);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::ORN_asimd(bool Q, Vec Vm, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = ir.VectorOr(operand1, ir.VectorNot(operand2));

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::SUB_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = ir.VectorSub(esize, operand1, operand2);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::CMEQ_reg_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);

    IR::U128 result = ir.VectorEqual(esize, operand1, operand2);

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::MLS_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    const IR::U128 operand3 = V(datasize, Vd);

    const IR::U128 result = ir.VectorSub(esize, operand3, ir.VectorMultiply(esize, operand1, operand2));

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::EOR_asimd(bool Q, Vec Vm, Vec Vn, Vec Vd) {
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = ir.VectorEor(operand1, operand2);

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::FMUL_vec_2(bool Q, bool sz, Vec Vm, Vec Vn, Vec Vd) {
    if (sz && !Q) {
        return ReservedValue();
    }
    const size_t esize = sz ? 64 : 32;
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    IR::U128 result = ir.FPVectorMul(esize, operand1, operand2);
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::FDIV_2(bool Q, bool sz, Vec Vm, Vec Vn, Vec Vd) {
    if (sz && !Q) {
        return ReservedValue();
    }
    const size_t esize = sz ? 64 : 32;
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);
    IR::U128 result = ir.FPVectorDiv(esize, operand1, operand2);
    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }
    V(datasize, Vd, result);
    return true;
}

bool TranslatorVisitor::BIF(bool Q, Vec Vm, Vec Vn, Vec Vd) {
  const size_t datasize = Q ? 128 : 64;

  auto operand1 = V(datasize, Vd);
  auto operand4 = V(datasize, Vn);
  auto operand3 = ir.VectorNot(V(datasize, Vm));

  auto result = ir.VectorEor(operand1, ir.VectorAnd(ir.VectorEor(operand1, operand4), operand3));

  V(datasize, Vd, result);

  return true;
}

bool TranslatorVisitor::BIT(bool Q, Vec Vm, Vec Vn, Vec Vd) {
  const size_t datasize = Q ? 128 : 64;

  auto operand1 = V(datasize, Vd);
  auto operand4 = V(datasize, Vn);
  auto operand3 = V(datasize, Vm);

  auto result = ir.VectorEor(operand1, ir.VectorAnd(ir.VectorEor(operand1, operand4), operand3));

  V(datasize, Vd, result);

  return true;
}

bool TranslatorVisitor::BSL(bool Q, Vec Vm, Vec Vn, Vec Vd) {
  const size_t datasize = Q ? 128 : 64;

  auto operand4 = V(datasize, Vn);
  auto operand1 = V(datasize, Vm);
  auto operand3 = V(datasize, Vd);

  auto result = ir.VectorEor(operand1, ir.VectorAnd(ir.VectorEor(operand1, operand4), operand3));

  V(datasize, Vd, result);

  return true;
}

} // namespace Dynarmic::A64
