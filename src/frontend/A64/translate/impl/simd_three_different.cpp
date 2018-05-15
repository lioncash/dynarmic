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

enum class Signedness {
    Signed,
    Unsigned
};

void AbsoluteDifferenceLong(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd,
                            AbsoluteDifferenceBehavior behavior, Signedness sign) {
    const size_t esize = 8 << size.ZeroExtend();
    const size_t datasize = 64;

    const IR::U128 operand1 = v.ir.VectorZeroExtend(esize, v.Vpart(datasize, Vn, Q));
    const IR::U128 operand2 = v.ir.VectorZeroExtend(esize, v.Vpart(datasize, Vm, Q));
    IR::U128 result = sign == Signedness::Signed ? v.ir.VectorSignedAbsoluteDifference(esize, operand1, operand2)
                                                 : v.ir.VectorUnsignedAbsoluteDifference(esize, operand1, operand2);

    if (behavior == AbsoluteDifferenceBehavior::Accumulate) {
        const IR::U128 data = v.V(2 * datasize, Vd);
        result = v.ir.VectorAdd(2 * esize, result, data);
    }

    v.V(2 * datasize, Vd, result);
}

enum class MultiplyLongBehavior {
    None,
    Accumulate,
    Subtract
};

void MultiplyLong(TranslatorVisitor& v, bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd,
                  MultiplyLongBehavior behavior, Signedness sign) {
    const size_t esize = 8 << size.ZeroExtend();
    const size_t doubled_esize = 2 * esize;
    const size_t datasize = 64;
    const size_t doubled_datasize = datasize * 2;

    const auto get_operands = [&] {
        const auto p1 = v.Vpart(datasize, Vn, Q);
        const auto p2 = v.Vpart(datasize, Vm, Q);

        if (sign == Signedness::Signed) {
            return std::make_pair(v.ir.VectorSignExtend(esize, p1),
                                  v.ir.VectorSignExtend(esize, p2));
        }

        return std::make_pair(v.ir.VectorZeroExtend(esize, p1),
                              v.ir.VectorZeroExtend(esize, p2));
    };

    const auto [operand1, operand2] = get_operands();
    IR::U128 result = v.ir.VectorMultiply(doubled_esize, operand1, operand2);

    if (behavior == MultiplyLongBehavior::Accumulate) {
        const IR::U128 addend = v.V(doubled_datasize, Vd);
        result = v.ir.VectorAdd(doubled_esize, addend, result);
    } else if (behavior == MultiplyLongBehavior::Subtract) {
        const IR::U128 minuend = v.V(doubled_datasize, Vd);
        result = v.ir.VectorSub(doubled_esize, minuend, result);
    }

    v.V(doubled_datasize, Vd, result);
}
} // Anonymous namespace

bool TranslatorVisitor::SABAL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    AbsoluteDifferenceLong(*this, Q, size, Vm, Vn, Vd, AbsoluteDifferenceBehavior::Accumulate, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SABDL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    AbsoluteDifferenceLong(*this, Q, size, Vm, Vn, Vd, AbsoluteDifferenceBehavior::None, Signedness::Signed);
    return true;
}

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

bool TranslatorVisitor::SMLAL_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    MultiplyLong(*this, Q, size, Vm, Vn, Vd, MultiplyLongBehavior::Accumulate, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SMLSL_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    MultiplyLong(*this, Q, size, Vm, Vn, Vd, MultiplyLongBehavior::Subtract, Signedness::Signed);
    return true;
}

bool TranslatorVisitor::SMULL_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    MultiplyLong(*this, Q, size, Vm, Vn, Vd, MultiplyLongBehavior::None, Signedness::Signed);
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
    
    AbsoluteDifferenceLong(*this, Q, size, Vm, Vn, Vd, AbsoluteDifferenceBehavior::Accumulate, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::UABDL(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    AbsoluteDifferenceLong(*this, Q, size, Vm, Vn, Vd, AbsoluteDifferenceBehavior::None, Signedness::Unsigned);
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

bool TranslatorVisitor::UMLAL_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    MultiplyLong(*this, Q, size, Vm, Vn, Vd, MultiplyLongBehavior::Accumulate, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::UMLSL_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    MultiplyLong(*this, Q, size, Vm, Vn, Vd, MultiplyLongBehavior::Subtract, Signedness::Unsigned);
    return true;
}

bool TranslatorVisitor::UMULL_vec(bool Q, Imm<2> size, Vec Vm, Vec Vn, Vec Vd) {
    if (size == 0b11) {
        return ReservedValue();
    }

    MultiplyLong(*this, Q, size, Vm, Vn, Vd, MultiplyLongBehavior::None, Signedness::Unsigned);
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
