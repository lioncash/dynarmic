/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

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
