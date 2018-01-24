/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/bit_util.h"
#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::DUP_gen(bool Q, Imm<5> imm5, Reg Rn, Vec Vd) {
    const size_t size = Common::LowestSetBit(imm5.ZeroExtend());
    if (size > 3) return UnallocatedEncoding();
    if (size == 3 && !Q) return ReservedValue();
    const size_t esize = 8 << size;
    const size_t datasize = Q ? 128 : 64;

    const IR::UAny element = X(esize, Rn);

    const IR::U128 result = [&]{
        switch (esize) {
        case 8:
            return Q ? ir.VectorBroadcast8(element) : ir.VectorLowerBroadcast8(element);
        case 16:
            return Q ? ir.VectorBroadcast16(element) : ir.VectorLowerBroadcast16(element);
        case 32:
            return Q ? ir.VectorBroadcast32(element) : ir.VectorLowerBroadcast32(element);
        default:
            return ir.VectorBroadcast64(element);
        }
    }();

    V(datasize, Vd, result);

    return true;
}

} // namespace A64
} // namespace Dynarmic
