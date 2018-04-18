/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <utility>
#include <boost/optional.hpp>

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {
namespace {
std::pair<size_t, Vec> Combine(Imm<2> size, bool H, bool L, bool M, Vec Vm) {
    if (size == 0b01) {
        return {H << 2 | L << 1 | M, Vm};
    }

    return {H << 1 | L, static_cast<Vec>(static_cast<int>(Vm) | M << 4)};
}
} // Anonymous namespace

bool TranslatorVisitor::MLA_elt(bool Q, Imm<2> size, bool L, bool M, Vec Vm, bool H, Vec Vn, Vec Vd) {
    if (size != 0b01 && size != 0b10) {
        return UnallocatedEncoding();
    }

    const size_t datasize = Q ? 128 : 64;
    const size_t indexsize = H ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend();

    const auto [index, full_vm] = Combine(size, H, L, M, Vm);

    const IR::U128 d = V(datasize, Vd);
    const IR::U128 m = V(indexsize, full_vm);
    const IR::U128 n = V(datasize, Vn);
    const IR::U128 index_vector = ir.VectorBroadcast(esize, ir.VectorGetElement(esize, m, index));
    const IR::U128 product = ir.VectorMultiply(esize, n, index_vector);
    const IR::U128 result = ir.VectorAdd(esize, d, product);

    V(datasize, Vd, result);
    return true;
}

} // namespace Dynarmic::A64
