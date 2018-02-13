/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <boost/optional.hpp>

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

static bool ExclusiveSharedDecodeAndOperation(TranslatorVisitor& tv, IREmitter& ir, bool pair, size_t size, bool L, bool o0, boost::optional<Reg> Rs, boost::optional<Reg> Rt2, Reg Rn, Reg Rt) {
    // Shared Decode

    const AccType acctype = o0 ? AccType::ORDERED : AccType::ATOMIC;
    const MemOp memop = L ? MemOp::LOAD : MemOp::STORE;
    const size_t elsize = 8 << size;
    const size_t regsize = elsize == 64 ? 64 : 32;
    const size_t datasize = pair ? elsize * 2 : elsize;

    // Operation

    const size_t dbytes = datasize / 8;

    if (memop == MemOp::LOAD && pair && Rt == *Rt2) {
        return tv.UnpredictableInstruction();
    } else if (memop == MemOp::STORE && (*Rs == Rt || (pair && *Rs == *Rt2))) {
        return tv.UnpredictableInstruction();
    } else if (memop == MemOp::STORE && *Rs == Rn && Rn != Reg::R31) {
        return tv.UnpredictableInstruction();
    }

    IR::U64 address;
    if (Rn == Reg::SP) {
        // TODO: Check SP Alignment
        address = tv.SP(64);
    } else {
        address = tv.X(64, Rn);
    }

    switch (memop) {
    case MemOp::STORE: {
        IR::UAnyU128 data;
        if (pair && elsize == 64) {
            data = ir.Pack2x64To1x128(tv.X(64, Rt), tv.X(64, *Rt2));
        } else if (pair && elsize == 32) {
            data = ir.Pack2x32To1x64(tv.X(32, Rt), tv.X(32, *Rt2));
        } else {
            data = tv.X(datasize, Rt);
        }
        IR::U32 status = tv.ExclusiveMem(address, dbytes, acctype, data);
        tv.X(32, *Rs, status);
        break;
    }
    case MemOp::LOAD: {
        ir.SetExclusive(address, dbytes);
        IR::UAnyU128 data = tv.Mem(address, dbytes, acctype);
        if (pair && elsize == 64) {
            tv.X(64, Rt, ir.VectorGetElement(64, data, 0));
            tv.X(64, *Rt2, ir.VectorGetElement(64, data, 1));
        } else if (pair && elsize == 32) {
            tv.X(32, Rt, ir.LeastSignificantWord(data));
            tv.X(32, *Rt2, ir.MostSignificantWord(data).result);
        } else {
            tv.X(regsize, Rt, tv.ZeroExtend(data, regsize));
        }
        break;
    }
    default:
        UNREACHABLE();
    }

    return true;
}

bool TranslatorVisitor::STXR(Imm<2> sz, Reg Rs, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, Rs, {}, Rn, Rt);
}

bool TranslatorVisitor::STLXR(Imm<2> sz, Reg Rs, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, Rs, {}, Rn, Rt);
}

bool TranslatorVisitor::STXP(Imm<1> sz, Reg Rs, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, Rs, Rt2, Rn, Rt);
}

bool TranslatorVisitor::STLXP(Imm<1> sz, Reg Rs, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, Rs, Rt2, Rn, Rt);
}

bool TranslatorVisitor::LDXR(Imm<2> sz, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, {}, {}, Rn, Rt);
}

bool TranslatorVisitor::LDAXR(Imm<2> sz, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, {}, {}, Rn, Rt);
}

bool TranslatorVisitor::LDXP(Imm<1> sz, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, {}, Rt2, Rn, Rt);
}

bool TranslatorVisitor::LDAXP(Imm<1> sz, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, ir, pair, size, L, o0, {}, Rt2, Rn, Rt);
}

static bool OrderedSharedDecodeAndOperation(TranslatorVisitor& tv, size_t size, bool L, bool o0, Reg Rn, Reg Rt) {
    // Shared Decode

    const AccType acctype = !o0 ? AccType::LIMITEDORDERED : AccType::ORDERED;
    const MemOp memop = L ? MemOp::LOAD : MemOp::STORE;
    const size_t elsize = 8 << size;
    const size_t regsize = elsize == 64 ? 64 : 32;
    const size_t datasize = elsize;

    // Operation

    const size_t dbytes = datasize / 8;

    IR::U64 address;
    if (Rn == Reg::SP) {
        // TODO: Check SP Alignment
        address = tv.SP(64);
    } else {
        address = tv.X(64, Rn);
    }

    switch (memop) {
    case MemOp::STORE: {
        IR::UAny data = tv.X(datasize, Rt);
        tv.Mem(address, dbytes, acctype, data);
        break;
    }
    case MemOp::LOAD: {
        IR::UAny data = tv.Mem(address, dbytes, acctype);
        tv.X(regsize, Rt, tv.ZeroExtend(data, regsize));
        break;
    }
    default:
        UNREACHABLE();
    }

    return true;
}

bool TranslatorVisitor::STLLR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 0;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

bool TranslatorVisitor::STLR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 1;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

bool TranslatorVisitor::LDLAR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 0;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

bool TranslatorVisitor::LDAR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 1;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

} // namespace Dynarmic::A64
