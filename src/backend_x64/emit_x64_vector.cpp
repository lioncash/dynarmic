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

namespace Dynarmic {
namespace BackendX64 {

using namespace Xbyak::util;

void EmitX64::EmitVectorGetElement8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);

    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();
        code->pextrb(dest, source, index);
        ctx.reg_alloc.DefineValue(inst, dest);
    } else {
        Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();
        code->pextrw(dest, source, index);
        ctx.reg_alloc.DefineValue(inst, dest);
    }
}

void EmitX64::EmitVectorGetElement16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
    Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();
    code->pextrw(dest, source, index);
    ctx.reg_alloc.DefineValue(inst, dest);
}

void EmitX64::EmitVectorGetElement32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    u8 index = args[1].GetImmediateU8();

    Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();

    if (index == 0) {
        Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
        code->movd(dest, source);
    } else if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
        code->pextrd(dest, source, index);
    } else {
        Xbyak::Xmm source = ctx.reg_alloc.UseScratchXmm(args[0]);
        code->pshufd(source, source, index);
        code->movd(dest, source);
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
        code->movq(dest, source);
    } else if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
        code->pextrq(dest, source, 1);
    } else {
        Xbyak::Xmm source = ctx.reg_alloc.UseScratchXmm(args[0]);
        code->punpckhqdq(source, source);
        code->movq(dest, source);
    }

    ctx.reg_alloc.DefineValue(inst, dest);
}

static void EmitVectorOperation(BlockOfCode* code, EmitContext& ctx, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Mmx& mmx, const Xbyak::Operand&)) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

    (code->*fn)(xmm_a, xmm_b);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
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

void EmitX64::EmitVectorLowerPairedAdd8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code->punpcklqdq(xmm_a, xmm_b);
    code->movdqa(tmp, xmm_a);
    code->psllw(xmm_a, 8);
    code->paddw(xmm_a, tmp);
    code->pxor(tmp, tmp);
    code->psrlw(xmm_a, 8);
    code->packuswb(xmm_a, tmp);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorLowerPairedAdd16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code->punpcklqdq(xmm_a, xmm_b);
    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        code->pxor(tmp, tmp);
        code->phaddw(xmm_a, tmp);
    } else {
        code->movdqa(tmp, xmm_a);
        code->pslld(xmm_a, 16);
        code->paddd(xmm_a, tmp);
        code->pxor(tmp, tmp);
        code->psrad(xmm_a, 16);
        code->packssdw(xmm_a, tmp); // Note: packusdw is SSE4.1, hence the arithmetic shift above.
    }

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorLowerPairedAdd32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code->punpcklqdq(xmm_a, xmm_b);
    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        code->pxor(tmp, tmp);
        code->phaddd(xmm_a, tmp);
    } else {
        code->movdqa(tmp, xmm_a);
        code->psllq(xmm_a, 32);
        code->paddq(xmm_a, tmp);
        code->psrlq(xmm_a, 32);
        code->pshufd(xmm_a, xmm_a, 0b11011000);
    }

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorPairedAdd8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm d = ctx.reg_alloc.ScratchXmm();

    code->movdqa(c, a);
    code->movdqa(d, b);
    code->psllw(a, 8);
    code->psllw(b, 8);
    code->paddw(a, c);
    code->paddw(b, d);
    code->psrlw(a, 8);
    code->psrlw(b, 8);
    code->packuswb(a, b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorPairedAdd16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

        code->phaddw(a, b);

        ctx.reg_alloc.DefineValue(inst, a);
    } else {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
        Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm d = ctx.reg_alloc.ScratchXmm();

        code->movdqa(c, a);
        code->movdqa(d, b);
        code->pslld(a, 16);
        code->pslld(b, 16);
        code->paddd(a, c);
        code->paddd(b, d);
        code->psrad(a, 16);
        code->psrad(b, 16);
        code->packssdw(a, b);

        ctx.reg_alloc.DefineValue(inst, a);
    }
}

void EmitX64::EmitVectorPairedAdd32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

        code->phaddd(a, b);

        ctx.reg_alloc.DefineValue(inst, a);
    } else {
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
        Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm d = ctx.reg_alloc.ScratchXmm();

        code->movdqa(c, a);
        code->movdqa(d, b);
        code->psllq(a, 32);
        code->psllq(b, 32);
        code->paddq(a, c);
        code->paddq(b, d);
        code->shufps(a, b, 0b11011101);

        ctx.reg_alloc.DefineValue(inst, a);
    }
}

void EmitX64::EmitVectorPairedAdd64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm c = ctx.reg_alloc.ScratchXmm();

    code->movdqa(c, a);
    code->punpcklqdq(a, b);
    code->punpckhqdq(c, b);
    code->paddq(a, c);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorLowerBroadcast8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        code->pxor(tmp, tmp);
        code->pshufb(a, tmp);
        code->movq(a, a);
    } else {
        code->punpcklbw(a, a);
        code->pshuflw(a, a, 0);
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorLowerBroadcast16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code->pshuflw(a, a, 0);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorLowerBroadcast32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code->pshuflw(a, a, 0b01000100);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        code->pxor(tmp, tmp);
        code->pshufb(a, tmp);
    } else {
        code->punpcklbw(a, a);
        code->pshuflw(a, a, 0);
        code->punpcklqdq(a, a);
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code->pshuflw(a, a, 0);
    code->punpcklqdq(a, a);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code->pshufd(a, a, 0);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code->punpcklqdq(a, a);

    ctx.reg_alloc.DefineValue(inst, a);
}


void EmitX64::EmitVectorZeroUpper(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code->movq(a, a); // TODO: !IsLastUse

    ctx.reg_alloc.DefineValue(inst, a);
}

} // namespace BackendX64
} // namespace Dynarmic
