/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;

template <typename Function>
static void EmitVectorOperation(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

    (code.*fn)(xmm_a, xmm_b);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorGetElement8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();
        code.pextrb(dest, source, index);
        ctx.reg_alloc.DefineValue(inst, dest);
    } else {
        Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();
        code.pextrw(dest, source, index / 2);
        if (index % 2 == 1) {
            code.shr(dest, 8);
        }
        ctx.reg_alloc.DefineValue(inst, dest);
    }
}

void EmitX64::EmitVectorGetElement16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
    Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();
    code.pextrw(dest, source, index);
    ctx.reg_alloc.DefineValue(inst, dest);
}

void EmitX64::EmitVectorGetElement32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();

    if (index == 0) {
        Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
        code.movd(dest, source);
    } else if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
        code.pextrd(dest, source, index);
    } else {
        Xbyak::Xmm source = ctx.reg_alloc.UseScratchXmm(args[0]);
        code.pshufd(source, source, index);
        code.movd(dest, source);
    }

    ctx.reg_alloc.DefineValue(inst, dest);
}

void EmitX64::EmitVectorGetElement64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Reg64 dest = ctx.reg_alloc.ScratchGpr().cvt64();

    if (index == 0) {
        Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
        code.movq(dest, source);
    } else if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
        code.pextrq(dest, source, 1);
    } else {
        Xbyak::Xmm source = ctx.reg_alloc.UseScratchXmm(args[0]);
        code.punpckhqdq(source, source);
        code.movq(dest, source);
    }

    ctx.reg_alloc.DefineValue(inst, dest);
}

void EmitX64::EmitVectorSetElement8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Reg8 source_elem = ctx.reg_alloc.UseGpr(args[2]).cvt8();

        code.pinsrb(source_vector, source_elem.cvt32(), index);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    } else {
        Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Reg32 source_elem = ctx.reg_alloc.UseScratchGpr(args[2]).cvt32();
        Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr().cvt32();

        code.pextrw(tmp, source_vector, index / 2);
        if (index % 2 == 0) {
            code.and_(tmp, 0xFF00);
            code.and_(source_elem, 0x00FF);
            code.or_(tmp, source_elem);
        } else {
            code.and_(tmp, 0x00FF);
            code.shl(source_elem, 8);
            code.or_(tmp, source_elem);
        }
        code.pinsrw(source_vector, tmp, index / 2);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    }
}

void EmitX64::EmitVectorSetElement16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg16 source_elem = ctx.reg_alloc.UseGpr(args[2]).cvt16();

    code.pinsrw(source_vector, source_elem.cvt32(), index);

    ctx.reg_alloc.DefineValue(inst, source_vector);
}

void EmitX64::EmitVectorSetElement32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Reg32 source_elem = ctx.reg_alloc.UseGpr(args[2]).cvt32();

        code.pinsrd(source_vector, source_elem, index);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    } else {
        Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Reg32 source_elem = ctx.reg_alloc.UseScratchGpr(args[2]).cvt32();

        code.pinsrw(source_vector, source_elem, index * 2);
        code.shr(source_elem, 16);
        code.pinsrw(source_vector, source_elem, index * 2 + 1);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    }
}

void EmitX64::EmitVectorSetElement64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Reg64 source_elem = ctx.reg_alloc.UseGpr(args[2]);

        code.pinsrq(source_vector, source_elem, index);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    } else {
        Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Reg64 source_elem = ctx.reg_alloc.UseScratchGpr(args[2]);

        code.pinsrw(source_vector, source_elem.cvt32(), index * 4);
        code.shr(source_elem, 16);
        code.pinsrw(source_vector, source_elem.cvt32(), index * 4 + 1);
        code.shr(source_elem, 16);
        code.pinsrw(source_vector, source_elem.cvt32(), index * 4 + 2);
        code.shr(source_elem, 16);
        code.pinsrw(source_vector, source_elem.cvt32(), index * 4 + 3);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    }
}

void EmitX64::EmitVectorAdd8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddb);
}

void EmitX64::EmitVectorAdd16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddw);
}

void EmitX64::EmitVectorAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddd);
}

void EmitX64::EmitVectorAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddq);
}

void EmitX64::EmitVectorAnd(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pand);
}

void EmitX64::EmitVectorBroadcastLower8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        code.pxor(tmp, tmp);
        code.pshufb(a, tmp);
        code.movq(a, a);
    } else {
        code.punpcklbw(a, a);
        code.pshuflw(a, a, 0);
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcastLower16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pshuflw(a, a, 0);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcastLower32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pshuflw(a, a, 0b01000100);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        code.pxor(tmp, tmp);
        code.pshufb(a, tmp);
    } else {
        code.punpcklbw(a, a);
        code.pshuflw(a, a, 0);
        code.punpcklqdq(a, a);
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pshuflw(a, a, 0);
    code.punpcklqdq(a, a);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pshufd(a, a, 0);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.punpcklqdq(a, a);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorEor(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pxor);
}

void EmitX64::EmitVectorEqual8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpeqb);
}

void EmitX64::EmitVectorEqual16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpeqw);
}

void EmitX64::EmitVectorEqual32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpeqd);
}

void EmitX64::EmitVectorEqual64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpeqq);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.pcmpeqd(xmm_a, xmm_b);
    code.pshufd(tmp, xmm_a, 0b10110001);
    code.pand(xmm_a, tmp);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorEqual128(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
        Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        code.pcmpeqq(xmm_a, xmm_b);
        code.pshufd(tmp, xmm_a, 0b01001110);
        code.pand(xmm_a, tmp);

        ctx.reg_alloc.DefineValue(inst, xmm_a);
    } else {
        Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
        Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        code.pcmpeqd(xmm_a, xmm_b);
        code.pshufd(tmp, xmm_a, 0b10110001);
        code.pand(xmm_a, tmp);
        code.pshufd(tmp, xmm_a, 0b01001110);
        code.pand(xmm_a, tmp);

        ctx.reg_alloc.DefineValue(inst, xmm_a);
    }
}

static void EmitVectorInterleaveLower(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, int size) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    switch (size) {
    case 8:
        code.punpcklbw(a, b);
        break;
    case 16:
        code.punpcklwd(a, b);
        break;
    case 32:
        code.punpckldq(a, b);
        break;
    case 64:
        code.punpcklqdq(a, b);
        break;
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorInterleaveLower8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveLower(code, ctx, inst, 8);
}

void EmitX64::EmitVectorInterleaveLower16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveLower(code, ctx, inst, 16);
}

void EmitX64::EmitVectorInterleaveLower32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveLower(code, ctx, inst, 32);
}

void EmitX64::EmitVectorInterleaveLower64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveLower(code, ctx, inst, 64);
}

void EmitX64::EmitVectorNot(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.ScratchXmm();

    code.pcmpeqw(xmm_b, xmm_b);
    code.pxor(xmm_a, xmm_b);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorOr(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::por);
}

void EmitX64::EmitVectorPairedAddLower8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.punpcklqdq(xmm_a, xmm_b);
    code.movdqa(tmp, xmm_a);
    code.psllw(xmm_a, 8);
    code.paddw(xmm_a, tmp);
    code.pxor(tmp, tmp);
    code.psrlw(xmm_a, 8);
    code.packuswb(xmm_a, tmp);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorPairedAddLower16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.punpcklqdq(xmm_a, xmm_b);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        code.pxor(tmp, tmp);
        code.phaddw(xmm_a, tmp);
    } else {
        code.movdqa(tmp, xmm_a);
        code.pslld(xmm_a, 16);
        code.paddd(xmm_a, tmp);
        code.pxor(tmp, tmp);
        code.psrad(xmm_a, 16);
        code.packssdw(xmm_a, tmp); // Note: packusdw is SSE4.1, hence the arithmetic shift above.
    }

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorPairedAddLower32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.punpcklqdq(xmm_a, xmm_b);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        code.pxor(tmp, tmp);
        code.phaddd(xmm_a, tmp);
    } else {
        code.movdqa(tmp, xmm_a);
        code.psllq(xmm_a, 32);
        code.paddq(xmm_a, tmp);
        code.psrlq(xmm_a, 32);
        code.pshufd(xmm_a, xmm_a, 0b11011000);
    }

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorPairedAdd8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm d = ctx.reg_alloc.ScratchXmm();

    code.movdqa(c, a);
    code.movdqa(d, b);
    code.psllw(a, 8);
    code.psllw(b, 8);
    code.paddw(a, c);
    code.paddw(b, d);
    code.psrlw(a, 8);
    code.psrlw(b, 8);
    code.packuswb(a, b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorPairedAdd16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

        code.phaddw(a, b);

        ctx.reg_alloc.DefineValue(inst, a);
    } else {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
        Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm d = ctx.reg_alloc.ScratchXmm();

        code.movdqa(c, a);
        code.movdqa(d, b);
        code.pslld(a, 16);
        code.pslld(b, 16);
        code.paddd(a, c);
        code.paddd(b, d);
        code.psrad(a, 16);
        code.psrad(b, 16);
        code.packssdw(a, b);

        ctx.reg_alloc.DefineValue(inst, a);
    }
}

void EmitX64::EmitVectorPairedAdd32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

        code.phaddd(a, b);

        ctx.reg_alloc.DefineValue(inst, a);
    } else {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
        Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm d = ctx.reg_alloc.ScratchXmm();

        code.movdqa(c, a);
        code.movdqa(d, b);
        code.psllq(a, 32);
        code.psllq(b, 32);
        code.paddq(a, c);
        code.paddq(b, d);
        code.shufps(a, b, 0b11011101);

        ctx.reg_alloc.DefineValue(inst, a);
    }
}

void EmitX64::EmitVectorPairedAdd64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();

    code.movdqa(c, a);
    code.punpcklqdq(a, b);
    code.punpckhqdq(c, b);
    code.paddq(a, c);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorLogicalShiftLeft8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    // TODO: Optimize
    for (size_t i = 0; i < shift_amount; ++i) {
        code.paddb(result, result);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorLogicalShiftLeft16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.psllw(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorLogicalShiftLeft32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.pslld(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorLogicalShiftLeft64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.psllq(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorLogicalShiftRight8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm mask = ctx.reg_alloc.ScratchXmm();
    const u8 shift_amount = args[1].GetImmediateU8();

    // TODO: Optimize
    code.pcmpeqb(mask, mask); // mask = 0xFF
    code.paddb(mask, mask); // mask = 0xFE
    code.pxor(zeros, zeros);
    for (size_t i = 0; i < shift_amount; ++i) {
        code.pand(result, mask);
        code.pavgb(result, zeros);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorLogicalShiftRight16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.psrlw(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorLogicalShiftRight32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.psrld(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorLogicalShiftRight64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.psrlq(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

static void EmitVectorZeroExtend(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, int size) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();

    code.pxor(zeros, zeros);
    switch (size) {
    case 8:
        code.punpcklbw(a, zeros);
        break;
    case 16:
        code.punpcklwd(a, zeros);
        break;
    case 32:
        code.punpckldq(a, zeros);
        break;
    case 64:
        code.punpcklqdq(a, zeros);
        break;
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorZeroExtend8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorZeroExtend(code, ctx, inst, 8);
}

void EmitX64::EmitVectorZeroExtend16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorZeroExtend(code, ctx, inst, 16);
}

void EmitX64::EmitVectorZeroExtend32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorZeroExtend(code, ctx, inst, 32);
}

void EmitX64::EmitVectorZeroExtend64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorZeroExtend(code, ctx, inst, 64);
}

void EmitX64::EmitVectorZeroUpper(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.movq(a, a); // TODO: !IsLastUse

    ctx.reg_alloc.DefineValue(inst, a);
}

} // namespace Dynarmic::BackendX64
