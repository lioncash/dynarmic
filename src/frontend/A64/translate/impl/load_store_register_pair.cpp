/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::STP_LDP_gen(Imm<2> opc, bool not_postindex, bool wback, Imm<1> L, Imm<7> imm7, Reg Rt2, Reg Rn, Reg Rt) {
    const bool postindex = !not_postindex;

    const MemOp memop = L == 1 ? MemOp::LOAD : MemOp::STORE;
    if ((L == 0 && opc.Bit<0>() == 1) || opc == 0b11)
        return UnallocatedEncoding();
    const bool signed_ = opc.Bit<0>() != 0;
    const size_t scale = 2 + opc.Bit<1>();
    const size_t datasize = 8 << scale;
    const u64 offset = imm7.SignExtend<u64>() << scale;

    if (memop == MemOp::LOAD && wback && (Rt == Rn || Rt2 == Rn) && Rn != Reg::R31)
        return UnpredictableInstruction();
    if (memop == MemOp::STORE && wback && (Rt == Rn || Rt2 == Rn) && Rn != Reg::R31)
        return UnpredictableInstruction();
    if (memop == MemOp::LOAD && Rt == Rt2)
        return UnpredictableInstruction();

    IR::U64 address;
    IR::U32U64 data1;
    IR::U32U64 data2;
    const size_t dbytes = datasize / 8;

    if (Rn == Reg::SP)
        // TODO: Check SP Alignment
        address = SP(64);
    else
        address = X(64, Rn);

    if (!postindex)
        address = ir.Add(address, ir.Imm64(offset));

    switch (memop) {
    case MemOp::STORE: {
        IR::U32U64 data1 = X(datasize, Rt);
        IR::U32U64 data2 = X(datasize, Rt2);
        Mem(address, dbytes, AccType::NORMAL, data1);
        Mem(ir.Add(address, ir.Imm64(dbytes)), dbytes, AccType::NORMAL, data2);
        break;
    }
    case MemOp::LOAD: {
        IR::U32U64 data1 = Mem(address, dbytes, AccType::NORMAL);
        IR::U32U64 data2 = Mem(ir.Add(address, ir.Imm64(dbytes)), dbytes, AccType::NORMAL);
        if (signed_) {
            X(64, Rt, SignExtend(data1, 64));
            X(64, Rt2, SignExtend(data2, 64));
        } else {
            X(datasize, Rt, data1);
            X(datasize, Rt2, data2);
        }
        break;
    }
    }

    if (wback) {
        if (postindex)
            address = ir.Add(address, ir.Imm64(offset));
        if (Rn == Reg::SP)
            SP(64, address);
        else
            X(64, Rn, address);
    }

    return true;
}

} // namespace A64
} // namespace Dynarmic
