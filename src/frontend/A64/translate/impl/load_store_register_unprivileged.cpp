/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

static bool store_register(TranslatorVisitor& tv, IREmitter& ir, const size_t datasize, Imm<9> imm9,
                           Reg Rn, Reg Rt) {
    const u64 offset = imm9.ZeroExtend<u64>();
    AccType acctype = AccType::UNPRIV;
    IR::U64 address;

    if (Rn == Reg::SP) {
        // TODO: Check Stack Alignment
        address = tv.SP(datasize);
    } else {
        address = tv.X(datasize, Rn);
    }
    address = ir.Add(address, ir.Imm64(offset));
    IR::UAny data = tv.X(datasize, Rt);
    tv.Mem(address, datasize / 8, acctype, data);
    return true;
}

static bool load_register(TranslatorVisitor& tv, IREmitter& ir, const size_t datasize, Imm<9> imm9,
                          Reg Rn, Reg Rt) {
    const u64 offset = imm9.ZeroExtend<u64>();
    AccType acctype = AccType::UNPRIV;
    IR::U64 address;

    if (Rn == Reg::SP) {
        // TODO: Check Stack Alignment
        address = tv.SP(datasize);
    } else {
        address = tv.X(datasize, Rn);
    }
    address = ir.Add(address, ir.Imm64(offset));
    IR::UAny data = tv.Mem(address, datasize / 8, acctype);
    tv.X(datasize, Rt, tv.ZeroExtend(data, 32));
    return true;
}

static bool load_register_signed(TranslatorVisitor& tv, IREmitter& ir, const size_t datasize,
                                 Imm<2> opc, Imm<9> imm9, Reg Rn, Reg Rt) {
    const u64 offset = imm9.ZeroExtend<u64>();
    AccType acctype = AccType::UNPRIV;
    MemOp memop;
    bool signed_;
    size_t regsize;
    if (opc.Bit<1>() == 0) {
        // store or zero-extending load
        memop = opc.Bit<0>() ? MemOp::LOAD : MemOp::STORE;
        regsize = 32;
        signed_ = false;
    } else {
        // sign-extending load
        memop = MemOp::LOAD;
        regsize = opc.Bit<0>() ? 32 : 64;
        signed_ = true;
    }

    IR::U64 address;
    if (Rn == Reg::SP) {
        // TODO: Check Stack Alignment
        address = tv.SP(datasize);
    } else {
        address = tv.X(datasize, Rn);
    }
    address = ir.Add(address, ir.Imm64(offset));

    switch (memop) {
    case MemOp::STORE:
        tv.Mem(address, datasize / 8, acctype, tv.X(datasize, Rt));
        break;
    case MemOp::LOAD: {
        IR::U8 data = tv.Mem(address, datasize / 8, acctype);
        if (signed_) {
            tv.X(datasize, Rt, tv.SignExtend(data, regsize));
        } else {
            tv.X(datasize, Rt, tv.ZeroExtend(data, regsize));
        }
        break;
    }
    case MemOp::PREFETCH:
        // Prefetch(address, Rt);
        break;
    }
    return true;
}

bool TranslatorVisitor::STTRB(Imm<9> imm9, Reg Rn, Reg Rt) {
    return store_register(*this, ir, 8, imm9, Rn, Rt);
}

bool TranslatorVisitor::STTRH(Imm<9> imm9, Reg Rn, Reg Rt) {
    return store_register(*this, ir, 16, imm9, Rn, Rt);
}

bool TranslatorVisitor::STTR(Imm<2> size, Imm<9> imm9, Reg Rn, Reg Rt) {
    const size_t scale = size.ZeroExtend<size_t>();
    const size_t datasize = 8 << scale;
    return store_register(*this, ir, datasize, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRB(Imm<9> imm9, Reg Rn, Reg Rt) {
    return load_register(*this, ir, 8, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRH(Imm<9> imm9, Reg Rn, Reg Rt) {
    return load_register(*this, ir, 16, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTR(Imm<2> size, Imm<9> imm9, Reg Rn, Reg Rt) {
    const size_t scale = size.ZeroExtend<size_t>();
    const size_t datasize = 8 << scale;
    return load_register(*this, ir, datasize, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRSB(Imm<2> opc, Imm<9> imm9, Reg Rn, Reg Rt) {
    return load_register_signed(*this, ir, 8, opc, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRSH(Imm<2> opc, Imm<9> imm9, Reg Rn, Reg Rt) {
    return load_register_signed(*this, ir, 16, opc, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRSW(Imm<9> imm9, Reg Rn, Reg Rt) {
    const u64 offset = imm9.ZeroExtend<u64>();
    AccType acctype = AccType::UNPRIV;
    IR::U64 address;

    if (Rn == Reg::SP) {
        // TODO: Check Stack Alignment
        address = SP(32);
    } else {
        address = X(32, Rn);
    }
    address = ir.Add(address, ir.Imm64(offset));
    IR::UAny data = Mem(address, 4, acctype);
    X(32, Rt, SignExtend(data, 64));
    return true;
}
} // namespace A64
} // namespace Dynarmic