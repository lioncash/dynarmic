/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

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

bool TranslatorVisitor::PRFM_imm([[maybe_unused]] Imm<12> imm12, [[maybe_unused]] Reg Rn, [[maybe_unused]] Reg Rt) {
    // Currently a NOP (which is valid behavior, as indicated by
    // the ARMv8 architecture reference manual)
    return true;
}

bool TranslatorVisitor::PRFM_unscaled_imm([[maybe_unused]] Imm<9> imm9, [[maybe_unused]] Reg Rn, [[maybe_unused]] Reg Rt) {
    // Currently a NOP (which is valid behavior, as indicated by
    // the ARMv8 architecture reference manual)
    return true;
}

static bool LoadStoreSIMD(TranslatorVisitor& v, IREmitter& ir, bool wback, bool postindex, size_t scale, u64 offset, MemOp memop, Reg Rn, Vec Vt) {
    const AccType acctype = AccType::VEC;
    const size_t datasize = 8 << scale;

    IR::U64 address;
    if (Rn == Reg::SP) {
        // TODO: Check SP Alignment
        address = v.SP(64);
    } else {
        address = v.X(64, Rn);
    }

    if (!postindex) {
        address = ir.Add(address, ir.Imm64(offset));
    }

    switch (memop) {
    case MemOp::STORE:
        if (datasize == 128) {
            const IR::U128 data = v.V(128, Vt);
            v.Mem(address, 16, acctype, data);
        } else {
            const IR::UAny data = ir.VectorGetElement(datasize, v.V(128, Vt), 0);
            v.Mem(address, datasize / 8, acctype, data);
        }
        break;
    case MemOp::LOAD:
        if (datasize == 128) {
            const IR::U128 data = v.Mem(address, 16, acctype);
            v.V(128, Vt, data);
        } else {
            const IR::UAny data = v.Mem(address, datasize / 8, acctype);
            v.V(128, Vt, ir.ZeroExtendToQuad(data));
        }
        break;
    default:
        UNREACHABLE();
    }

    if (wback) {
        if (postindex) {
            address = ir.Add(address, ir.Imm64(offset));
        }
        if (Rn == Reg::SP) {
            v.SP(64, address);
        } else {
            v.X(64, Rn, address);
        }
    }

    return true;
}

bool TranslatorVisitor::STR_imm_fpsimd_1(Imm<2> size, Imm<1> opc_1, Imm<9> imm9, bool not_postindex, Reg Rn, Vec Vt) {
    const bool wback = true;
    const bool postindex = !not_postindex;
    const size_t scale = concatenate(opc_1, size).ZeroExtend<size_t>();
    if (scale > 4) return UnallocatedEncoding();
    const u64 offset = imm9.SignExtend<u64>();

    return LoadStoreSIMD(*this, ir, wback, postindex, scale, offset, MemOp::STORE, Rn, Vt);
}

bool TranslatorVisitor::STR_imm_fpsimd_2(Imm<2> size, Imm<1> opc_1, Imm<12> imm12, Reg Rn, Vec Vt) {
    const bool wback = false;
    const bool postindex = false;
    const size_t scale = concatenate(opc_1, size).ZeroExtend<size_t>();
    if (scale > 4) return UnallocatedEncoding();
    const u64 offset = imm12.ZeroExtend<u64>() << scale;

    return LoadStoreSIMD(*this, ir, wback, postindex, scale, offset, MemOp::STORE, Rn, Vt);
}

bool TranslatorVisitor::LDR_imm_fpsimd_1(Imm<2> size, Imm<1> opc_1, Imm<9> imm9, bool not_postindex, Reg Rn, Vec Vt) {
    const bool wback = true;
    const bool postindex = !not_postindex;
    const size_t scale = concatenate(opc_1, size).ZeroExtend<size_t>();
    if (scale > 4) return UnallocatedEncoding();
    const u64 offset = imm9.SignExtend<u64>();

    return LoadStoreSIMD(*this, ir, wback, postindex, scale, offset, MemOp::LOAD, Rn, Vt);
}

bool TranslatorVisitor::LDR_imm_fpsimd_2(Imm<2> size, Imm<1> opc_1, Imm<12> imm12, Reg Rn, Vec Vt) {
    const bool wback = false;
    const bool postindex = false;
    const size_t scale = concatenate(opc_1, size).ZeroExtend<size_t>();
    if (scale > 4) return UnallocatedEncoding();
    const u64 offset = imm12.ZeroExtend<u64>() << scale;

    return LoadStoreSIMD(*this, ir, wback, postindex, scale, offset, MemOp::LOAD, Rn, Vt);
}

bool TranslatorVisitor::STUR_fpsimd(Imm<2> size, Imm<1> opc_1, Imm<9> imm9, Reg Rn, Vec Vt) {
    const bool wback = false;
    const bool postindex = false;
    const size_t scale = concatenate(opc_1, size).ZeroExtend<size_t>();
    if (scale > 4) return UnallocatedEncoding();
    const u64 offset = imm9.SignExtend<u64>();

    return LoadStoreSIMD(*this, ir, wback, postindex, scale, offset, MemOp::STORE, Rn, Vt);
}

bool TranslatorVisitor::LDUR_fpsimd(Imm<2> size, Imm<1> opc_1, Imm<9> imm9, Reg Rn, Vec Vt) {
    const bool wback = false;
    const bool postindex = false;
    const size_t scale = concatenate(opc_1, size).ZeroExtend<size_t>();
    if (scale > 4) return UnallocatedEncoding();
    const u64 offset = imm9.SignExtend<u64>();

    return LoadStoreSIMD(*this, ir, wback, postindex, scale, offset, MemOp::LOAD, Rn, Vt);
}

} // namespace Dynarmic::A64
