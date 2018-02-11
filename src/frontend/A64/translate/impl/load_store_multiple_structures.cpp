/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <tuple>

#include <boost/optional.hpp>

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

static bool SharedDecodeAndOperation(TranslatorVisitor& tv, IREmitter& ir, bool wback, MemOp memop, bool Q, boost::optional<Reg> Rm, Imm<4> opcode, Imm<2> size, Reg Rn, Vec Vt) {
    const size_t datasize = Q ? 128 : 64;
    const size_t esize = 8 << size.ZeroExtend<size_t>();
    const size_t elements = datasize / esize;
    const size_t ebytes = esize / 8;

    size_t rpt, selem;
    switch (opcode.ZeroExtend()) {
    case 0b0000:
        rpt = 1;
        selem = 4;
        break;
    case 0b0010:
        rpt = 4;
        selem = 1;
        break;
    case 0b0100:
        rpt = 1;
        selem = 3;
        break;
    case 0b0110:
        rpt = 3;
        selem = 1;
        break;
    case 0b0111:
        rpt = 1;
        selem = 1;
        break;
    case 0b1000:
        rpt = 1;
        selem = 2;
        break;
    case 0b1010:
        rpt = 2;
        selem = 1;
        break;
    default:
        return tv.UnallocatedEncoding();
    }
    ASSERT(rpt == 1 || selem == 1);

    if ((size == 0b11 && !Q) && selem != 1) {
        return tv.ReservedValue();
    }

    IR::U64 address;
    if (Rn == Reg::SP)
        // TODO: Check SP Alignment
        address = tv.SP(64);
    else
        address = tv.X(64, Rn);

    IR::U64 offs = ir.Imm64(0);
    if (selem == 1) {
        for (size_t r = 0; r < rpt; r++) {
            const Vec tt = static_cast<Vec>((VecNumber(Vt) + r) % 32);
            if (memop == MemOp::LOAD) {
                const IR::UAnyU128 vec = tv.Mem(ir.Add(address, offs), ebytes * elements, AccType::VEC);
                tv.V_scalar(datasize, tt, vec);
            } else {
                const IR::UAnyU128 vec = tv.V_scalar(datasize, tt);
                tv.Mem(ir.Add(address, offs), ebytes * elements, AccType::VEC, vec);
            }
            offs = ir.Add(offs, ir.Imm64(ebytes * elements));
        }
    } else {
        for (size_t e = 0; e < elements; e++) {
            for (size_t s = 0; s < selem; s++) {
                const Vec tt = static_cast<Vec>((VecNumber(Vt) + s) % 32);
                if (memop == MemOp::LOAD) {
                    const IR::UAny elem = tv.Mem(ir.Add(address, offs), ebytes, AccType::VEC);
                    const IR::U128 vec = ir.VectorSetElement(esize, tv.V(datasize, tt), e, elem);
                    tv.V(datasize, tt, vec);
                } else {
                    const IR::UAny elem = ir.VectorGetElement(esize, tv.V(datasize, tt), e);
                    tv.Mem(ir.Add(address, offs), ebytes, AccType::VEC, elem);
                }
                offs = ir.Add(offs, ir.Imm64(ebytes));
            }
        }
    }

    if (wback) {
        if (*Rm != Reg::SP)
            offs = tv.X(64, *Rm);
        if (Rn == Reg::SP)
            tv.SP(64, ir.Add(address, offs));
        else
            tv.X(64, Rn, ir.Add(address, offs));
    }

    return true;
}

bool TranslatorVisitor::STx_mult_1(bool Q, Imm<4> opcode, Imm<2> size, Reg Rn, Vec Vt) {
    const bool wback = false;
    const MemOp memop = MemOp::STORE;
    return SharedDecodeAndOperation(*this, ir, wback, memop, Q, {}, opcode, size, Rn, Vt);
}

bool TranslatorVisitor::STx_mult_2(bool Q, Reg Rm, Imm<4> opcode, Imm<2> size, Reg Rn, Vec Vt) {
    const bool wback = true;
    const MemOp memop = MemOp::STORE;
    return SharedDecodeAndOperation(*this, ir, wback, memop, Q, Rm, opcode, size, Rn, Vt);
}

bool TranslatorVisitor::LDx_mult_1(bool Q, Imm<4> opcode, Imm<2> size, Reg Rn, Vec Vt) {
    const bool wback = false;
    const MemOp memop = MemOp::LOAD;
    return SharedDecodeAndOperation(*this, ir, wback, memop, Q, {}, opcode, size, Rn, Vt);
}

bool TranslatorVisitor::LDx_mult_2(bool Q, Reg Rm, Imm<4> opcode, Imm<2> size, Reg Rn, Vec Vt) {
    const bool wback = true;
    const MemOp memop = MemOp::LOAD;
    return SharedDecodeAndOperation(*this, ir, wback, memop, Q, Rm, opcode, size, Rn, Vt);
}

} // namespace Dynarmic::A64
