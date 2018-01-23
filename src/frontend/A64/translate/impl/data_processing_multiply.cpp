/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::MADD(bool sf, Reg Rm, Reg Ra, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    const IR::U32U64 a = X(datasize, Ra);
    const IR::U32U64 m = X(datasize, Rm);
    const IR::U32U64 n = X(datasize, Rn);

    const IR::U32U64 result = ir.Add(a, ir.Mul(n, m));

    X(datasize, Rd, result);
    return true;
}

bool TranslatorVisitor::MSUB(bool sf, Reg Rm, Reg Ra, Reg Rn, Reg Rd) {
    const size_t datasize = sf ? 64 : 32;

    const IR::U32U64 a = X(datasize, Ra);
    const IR::U32U64 m = X(datasize, Rm);
    const IR::U32U64 n = X(datasize, Rn);

    const IR::U32U64 result = ir.Sub(a, ir.Mul(n, m));

    X(datasize, Rd, result);
    return true;
}

} // namespace A64
} // namespace Dynarmic
