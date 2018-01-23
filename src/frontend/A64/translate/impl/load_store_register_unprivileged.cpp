/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

static bool StoreRegister(TranslatorVisitor& tv, IREmitter& ir, const size_t datasize,
                          const Imm<9> imm9, const Reg Rn, const Reg Rt) {
    const u64 offset = imm9.SignExtend<u64>();
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

static bool LoadRegister(TranslatorVisitor& tv, IREmitter& ir, const size_t datasize,
                         const Imm<9> imm9, const Reg Rn, const Reg Rt) {
    const u64 offset = imm9.SignExtend<u64>();
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
    // max is used to zeroextend < 32 to 32, and > 32 to 64
    const size_t extended_size = std::max<size_t>(32, datasize);
    tv.X(extended_size, Rt, tv.ZeroExtend(data, extended_size));
    return true;
}

static bool LoadRegisterSigned(TranslatorVisitor& tv, IREmitter& ir, const size_t datasize,
                               const Imm<2> opc, const Imm<9> imm9, const Reg Rn, const Reg Rt) {
    const u64 offset = imm9.SignExtend<u64>();
    AccType acctype = AccType::UNPRIV;
    MemOp memop;
    bool is_signed;
    size_t regsize;
    if (opc.Bit<1>() == 0) {
        // store or zero-extending load
        memop = opc.Bit<0>() ? MemOp::LOAD : MemOp::STORE;
        regsize = 32;
        is_signed = false;
    } else {
        // sign-extending load
        memop = MemOp::LOAD;
        regsize = opc.Bit<0>() ? 32 : 64;
        is_signed = true;
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
        IR::UAny data = tv.Mem(address, datasize / 8, acctype);
        if (is_signed) {
            tv.X(regsize, Rt, tv.SignExtend(data, regsize));
        } else {
            tv.X(regsize, Rt, tv.ZeroExtend(data, regsize));
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
    return StoreRegister(*this, ir, 8, imm9, Rn, Rt);
}

bool TranslatorVisitor::STTRH(Imm<9> imm9, Reg Rn, Reg Rt) {
    return StoreRegister(*this, ir, 16, imm9, Rn, Rt);
}

bool TranslatorVisitor::STTR(Imm<2> size, Imm<9> imm9, Reg Rn, Reg Rt) {
    const size_t scale = size.ZeroExtend<size_t>();
    const size_t datasize = 8 << scale;
    return StoreRegister(*this, ir, datasize, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRB(Imm<9> imm9, Reg Rn, Reg Rt) {
    return LoadRegister(*this, ir, 8, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRH(Imm<9> imm9, Reg Rn, Reg Rt) {
    return LoadRegister(*this, ir, 16, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTR(Imm<2> size, Imm<9> imm9, Reg Rn, Reg Rt) {
    const size_t scale = size.ZeroExtend<size_t>();
    const size_t datasize = 8 << scale;
    return LoadRegister(*this, ir, datasize, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRSB(Imm<2> opc, Imm<9> imm9, Reg Rn, Reg Rt) {
    return LoadRegisterSigned(*this, ir, 8, opc, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRSH(Imm<2> opc, Imm<9> imm9, Reg Rn, Reg Rt) {
    return LoadRegisterSigned(*this, ir, 16, opc, imm9, Rn, Rt);
}

bool TranslatorVisitor::LDTRSW(Imm<9> imm9, Reg Rn, Reg Rt) {
    const u64 offset = imm9.ZeroExtend<u64>();
    AccType acctype = AccType::UNPRIV;
    IR::U64 address;

    if (Rn == Reg::SP) {
        // TODO: Check Stack Alignment
        address = SP(64);
    } else {
        address = X(64, Rn);
    }
    address = ir.Add(address, ir.Imm64(offset));
    IR::UAny data = Mem(address, 4, acctype);
    X(64, Rt, SignExtend(data, 64));
    return true;
}
} // namespace A64
} // namespace Dynarmic
