/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/mp.h"
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

template <typename Lambda>
static void EmitTwoArgumentFallback(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    const auto fn = static_cast<mp::equivalent_function_type_t<Lambda>*>(lambda);
    constexpr u32 stack_space = 3 * 16;
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm arg1 = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm arg2 = ctx.reg_alloc.UseXmm(args[1]);
    ctx.reg_alloc.EndOfAllocScope();

    ctx.reg_alloc.HostCall(nullptr);
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE + 2 * 16]);

    code.movaps(xword[code.ABI_PARAM2], arg1);
    code.movaps(xword[code.ABI_PARAM3], arg2);
    code.CallFunction(+fn);
    code.movaps(xmm0, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);

    ctx.reg_alloc.DefineValue(inst, xmm0);
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

void EmitX64::EmitVectorArithmeticShiftRight8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
    const u8 shift_amount = args[1].GetImmediateU8();

    // TODO: Optimize
    code.movdqa(tmp, result);
    code.pslldq(tmp, 1);
    code.psraw(tmp, shift_amount);
    code.psraw(result, shift_amount + 8);
    code.psllw(result, 8);
    code.psrlw(tmp, 8);
    code.por(result, tmp);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorArithmeticShiftRight16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.psraw(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorArithmeticShiftRight32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    code.psrad(result, shift_amount);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorArithmeticShiftRight64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm tmp2 = ctx.reg_alloc.ScratchXmm();
    const u8 shift_amount = std::min(args[1].GetImmediateU8(), u8(63));

    const u64 sign_bit = 0x80000000'00000000u >> shift_amount;

    code.pxor(tmp2, tmp2);
    code.psrlq(result, shift_amount);
    code.movdqa(tmp1, code.MConst(sign_bit, sign_bit));
    code.pand(tmp1, result);
    code.psubq(tmp2, tmp1);
    code.por(result, tmp2);

    ctx.reg_alloc.DefineValue(inst, result);
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

void EmitX64::EmitVectorMultiply8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Xmm tmp_a = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();

    // TODO: Optimize
    code.movdqa(tmp_a, a);
    code.movdqa(tmp_b, b);
    code.pmullw(a, b);
    code.psrlw(tmp_a, 8);
    code.psrlw(tmp_b, 8);
    code.pmullw(tmp_a, tmp_b);
    code.pand(a, code.MConst(0x00FF00FF00FF00FF, 0x00FF00FF00FF00FF));
    code.psllw(tmp_a, 8);
    code.por(a, tmp_a);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorMultiply16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmullw);
}

void EmitX64::EmitVectorMultiply32(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmulld);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u32, 4>& result, const std::array<u32, 4>& a, const std::array<u32, 4>& b){
        for (size_t i = 0; i < 4; ++i) {
            result[i] = a[i] * b[i];
        }
    });
}

void EmitX64::EmitVectorMultiply64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);
        Xbyak::Reg64 tmp1 = ctx.reg_alloc.ScratchGpr();
        Xbyak::Reg64 tmp2 = ctx.reg_alloc.ScratchGpr();

        code.movq(tmp1, a);
        code.movq(tmp2, b);
        code.imul(tmp2, tmp1);
        code.pextrq(tmp1, a, 1);
        code.movq(a, tmp2);
        code.pextrq(tmp2, b, 1);
        code.imul(tmp1, tmp2);
        code.pinsrq(a, tmp1, 1);

        ctx.reg_alloc.DefineValue(inst, a);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u64, 2>& result, const std::array<u64, 2>& a, const std::array<u64, 2>& b){
        for (size_t i = 0; i < 2; ++i) {
            result[i] = a[i] * b[i];
        }
    });
}

void EmitX64::EmitVectorNarrow16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();

    // TODO: AVX512F implementation

    code.pxor(zeros, zeros);
    code.pand(a, code.MConst(0x00FF00FF00FF00FF, 0x00FF00FF00FF00FF));
    code.packuswb(a, zeros);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorNarrow32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();

    // TODO: AVX512F implementation

    code.pxor(zeros, zeros);
    code.pand(a, code.MConst(0x0000FFFF0000FFFF, 0x0000FFFF0000FFFF));
    code.packusdw(a, zeros);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorNarrow64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();

    // TODO: AVX512F implementation

    code.pxor(zeros, zeros);
    code.shufps(a, zeros, 0b00001000);

    ctx.reg_alloc.DefineValue(inst, a);
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

void EmitX64::EmitVectorPopulationCount(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        Xbyak::Xmm low_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm high_a = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm tmp2 = ctx.reg_alloc.ScratchXmm();

        code.movdqa(high_a, low_a);
        code.psrlw(high_a, 4);
        code.movdqa(tmp1, code.MConst(0x0F0F0F0F0F0F0F0F, 0x0F0F0F0F0F0F0F0F));
        code.pand(high_a, tmp1); // High nibbles
        code.pand(low_a, tmp1); // Low nibbles

        code.movdqa(tmp1, code.MConst(0x0302020102010100, 0x0403030203020201));
        code.movdqa(tmp2, tmp1);
        code.pshufb(tmp1, low_a);
        code.pshufb(tmp2, high_a);

        code.paddb(tmp1, tmp2);

        ctx.reg_alloc.DefineValue(inst, tmp1);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u8, 16>& result, const std::array<u8, 16>& a){
        for (size_t i = 0; i < 16; ++i) {
            result[i] = static_cast<u8>(Common::BitCount(a[i]));
        }
    });
}

void EmitX64::EmitVectorSub8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::psubb);
}

void EmitX64::EmitVectorSub16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::psubw);
}

void EmitX64::EmitVectorSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::psubd);
}

void EmitX64::EmitVectorSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::psubq);
}

void EmitX64::EmitVectorZeroExtend8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.pmovzxbw(a, a);
    } else {
        const Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();
        code.pxor(zeros, zeros);
        code.punpcklbw(a, zeros);
    }
    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorZeroExtend16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.pmovzxwd(a, a);
    } else {
        const Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();
        code.pxor(zeros, zeros);
        code.punpcklwd(a, zeros);
    }
    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorZeroExtend32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.pmovzxdq(a, a);
    } else {
        const Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();
        code.pxor(zeros, zeros);
        code.punpckldq(a, zeros);
    }
    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorZeroExtend64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();
    code.pxor(zeros, zeros);
    code.punpcklqdq(a, zeros);
    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorZeroUpper(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.movq(a, a); // TODO: !IsLastUse

    ctx.reg_alloc.DefineValue(inst, a);
}

} // namespace Dynarmic::BackendX64
