/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::ADD_vector(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    auto operand1 = V(datasize, Vn);
    auto operand2 = V(datasize, Vm);

    auto result = [&]{
        switch (esize) {
        case 8:
            return ir.VectorAdd8(operand1, operand2);
        case 16:
            return ir.VectorAdd16(operand1, operand2);
        case 32:
            return ir.VectorAdd32(operand1, operand2);
        default:
            return ir.VectorAdd64(operand1, operand2);
        }
    }();

    V(datasize, Vd, result);

    return true;
}

bool TranslatorVisitor::ADDP_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);

    const IR::U128 result = [&]{
        switch (esize) {
        case 8:
            return Q ? ir.VectorPairedAdd8(operand1, operand2) : ir.VectorLowerPairedAdd8(operand1, operand2);
        case 16:
            return Q ? ir.VectorPairedAdd16(operand1, operand2) : ir.VectorLowerPairedAdd16(operand1, operand2);
        case 32:
            return Q ? ir.VectorPairedAdd32(operand1, operand2) : ir.VectorLowerPairedAdd32(operand1, operand2);
        default:
            return ir.VectorPairedAdd64(operand1, operand2);
        }
    }();

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

bool TranslatorVisitor::CMEQ_reg_2(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11 && !Q) return ReservedValue();
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t datasize = Q ? 128 : 64;

    const IR::U128 operand1 = V(datasize, Vn);
    const IR::U128 operand2 = V(datasize, Vm);

    IR::U128 result = [&]{
        switch (esize) {
        case 8:
            return ir.VectorEqual8(operand1, operand2);
        case 16:
            return ir.VectorEqual16(operand1, operand2);
        case 32:
            return ir.VectorEqual32(operand1, operand2);
        default:
            return ir.VectorEqual64(operand1, operand2);
        }
    }();

    if (datasize == 64) {
        result = ir.VectorZeroUpper(result);
    }

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

  auto result = ir.VectorEor(operand1,
                    ir.VectorAnd(ir.VectorEor(operand1, operand4), operand3));

  V(datasize, Vd, result);

  return true;
}

bool TranslatorVisitor::BIT(bool Q, Vec Vm, Vec Vn, Vec Vd) {
  const size_t datasize = Q ? 128 : 64;

  auto operand1 = V(datasize, Vd);
  auto operand4 = V(datasize, Vn);
  auto operand3 = V(datasize, Vm);

  auto result = ir.VectorEor(operand1,
                    ir.VectorAnd(ir.VectorEor(operand1, operand4), operand3));

  V(datasize, Vd, result);

  return true;
}

bool TranslatorVisitor::BSL(bool Q, Vec Vm, Vec Vn, Vec Vd) {
  const size_t datasize = Q ? 128 : 64;

  auto operand4 = V(datasize, Vn);
  auto operand1 = V(datasize, Vm);
  auto operand3 = V(datasize, Vd);

  auto result = ir.VectorEor(operand1,
                    ir.VectorAnd(ir.VectorEor(operand1, operand4), operand3));

  V(datasize, Vd, result);

  return true;
}

} // namespace Dynarmic::A64
