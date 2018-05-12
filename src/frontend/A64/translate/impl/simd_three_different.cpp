/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
enum class AbsoluteDifferenceBehavior {
    None,
    Accumulate
};

void UnsignedAbsoluteDifferenceLong(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd,
                                    AbsoluteDifferenceBehavior behavior) {
    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = 64;

    const IR::U128 operand1 = v.ir.VectorZeroExtend(esize, v.Vpart(datasize, Vn, Q));
    const IR::U128 operand2 = v.ir.VectorZeroExtend(esize, v.Vpart(datasize, Vm, Q));
    IR::U128 result = v.ir.VectorUnsignedAbsoluteDifference(esize, operand1, operand2);

    if (behavior == AbsoluteDifferenceBehavior::Accumulate) {
        const IR::U128 data = v.V(2 * datasize, Vd);
        result = v.ir.VectorAdd(2 * esize, result, data);
    }

    v.V(2 * datasize, Vd, result);
}
} // Anonymous namespace

bool TranslatorVisitor::SADDL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = ir.VectorSignExtend(esize, Vpart(64, Vn, part));
    const IR::U128 operand2 = ir.VectorSignExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorAdd(esize * 2, operand1, operand2);

    V(128, Vd, result);
    return true;
}

bool TranslatorVisitor::SADDW(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = V(128, Vn);
    const IR::U128 operand2 = ir.VectorSignExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorAdd(esize * 2, operand1, operand2);

    V(128, Vd, result);
    return true;
}

bool TranslatorVisitor::SSUBW(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = V(128, Vn);
    const IR::U128 operand2 = ir.VectorSignExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorSub(esize * 2, operand1, operand2);

    V(128, Vd, result);
    return true;
}

bool TranslatorVisitor::SSUBL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = ir.VectorSignExtend(esize, Vpart(64, Vn, part));
    const IR::U128 operand2 = ir.VectorSignExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorSub(esize * 2, operand1, operand2);

    V(128, Vd, result);
    return true;
}

bool TranslatorVisitor::UADDL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = ir.VectorZeroExtend(esize, Vpart(64, Vn, part));
    const IR::U128 operand2 = ir.VectorZeroExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorAdd(esize * 2, operand1, operand2);

    V(128, Vd, result);
    return true;
}

bool TranslatorVisitor::UABAL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }
    
    UnsignedAbsoluteDifferenceLong(*this, Q, size, Vm, Vn, Vd, AbsoluteDifferenceBehavior::Accumulate);
    return true;
}

bool TranslatorVisitor::UABDL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    UnsignedAbsoluteDifferenceLong(*this, Q, size, Vm, Vn, Vd, AbsoluteDifferenceBehavior::None);
    return true;
}

bool TranslatorVisitor::UADDW(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = V(128, Vn);
    const IR::U128 operand2 = ir.VectorZeroExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorAdd(esize * 2, operand1, operand2);

    V(128, Vd, result);

    return true;
}

bool TranslatorVisitor::USUBW(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = V(128, Vn);
    const IR::U128 operand2 = ir.VectorZeroExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorSub(esize * 2, operand1, operand2);

    V(128, Vd, result);

    return true;
}

bool TranslatorVisitor::USUBL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    const size_t esize = 8 << size.ZeroExtend();
    const size_t part = Q ? 1 : 0;

    const IR::U128 operand1 = ir.VectorZeroExtend(esize, Vpart(64, Vn, part));
    const IR::U128 operand2 = ir.VectorZeroExtend(esize, Vpart(64, Vm, part));
    const IR::U128 result = ir.VectorSub(esize * 2, operand1, operand2);

    V(128, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
