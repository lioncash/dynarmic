/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

bool TranslatorVisitor::load_store_register_immediate(bool wback, bool postindex, size_t scale, u64 offset, Imm<2> size, Imm<2> opc, Reg Rn, Reg Rt) {
    MemOp memop;
    bool signed_ = false;
    size_t regsize = 0;

    if (opc.Bit<1>() == 0) {
        memop = opc.Bit<0>() ? MemOp::LOAD : MemOp::STORE;
        regsize = size == 0b11 ? 64 : 32;
        signed_ = false;
    } else if (size == 0b11) {
        memop = MemOp::PREFETCH;
        ASSERT(!opc.Bit<0>());
    } else {
        memop = MemOp::LOAD;
        ASSERT(!(size == 0b10 && opc.Bit<0>() == 1));
        regsize = opc.Bit<0>() ? 32 : 64;
        signed_ = true;
    }

    const size_t datasize = 8 << scale;

    if (memop == MemOp::LOAD && wback && Rn == Rt && Rn != Reg::R31) {
        return UnpredictableInstruction();
    }
    if (memop == MemOp::STORE && wback && Rn == Rt && Rn != Reg::R31) {
        return UnpredictableInstruction();
    }

    // TODO: Check SP alignment
    IR::U64 address = Rn == Reg::SP ? IR::U64(SP(64)) : IR::U64(X(64, Rn));

    if (!postindex)
        address = ir.Add(address, ir.Imm64(offset));

    switch (memop) {
    case MemOp::STORE: {
        auto data = X(datasize, Rt);
        Mem(address, datasize / 8, AccType::NORMAL, data);
        break;
    }
    case MemOp::LOAD: {
        auto data = Mem(address, datasize / 8, AccType::NORMAL);
        if (signed_)
            X(regsize, Rt, SignExtend(data, regsize));
        else
            X(regsize, Rt, ZeroExtend(data, regsize));
        break;
    }
    case MemOp::PREFETCH:
        // Prefetch(address, Rt)
        break;
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

bool TranslatorVisitor::STRx_LDRx_imm_1(Imm<2> size, Imm<2> opc, Imm<9> imm9, bool not_postindex, Reg Rn, Reg Rt) {
    const bool wback = true;
    const bool postindex = !not_postindex;
    const size_t scale = size.ZeroExtend<size_t>();
    const u64 offset = imm9.SignExtend<u64>();

    return load_store_register_immediate(wback, postindex, scale, offset, size, opc, Rn, Rt);
}

bool TranslatorVisitor::STRx_LDRx_imm_2(Imm<2> size, Imm<2> opc, Imm<12> imm12, Reg Rn, Reg Rt) {
    const bool wback = false;
    const bool postindex = false;
    const size_t scale = size.ZeroExtend<size_t>();
    const u64 offset = imm12.ZeroExtend<u64>() << scale;

    return load_store_register_immediate(wback, postindex, scale, offset, size, opc, Rn, Rt);
}

bool TranslatorVisitor::STURx_LDURx(Imm<2> size, Imm<2> opc, Imm<9> imm9, Reg Rn, Reg Rt) {
    const bool wback = false;
    const bool postindex = false;
    const size_t scale = size.ZeroExtend<size_t>();
    const u64 offset = imm9.SignExtend<u64>();

    return load_store_register_immediate(wback, postindex, scale, offset, size, opc, Rn, Rt);
}

} // namespace A64
} // namespace Dynarmic
