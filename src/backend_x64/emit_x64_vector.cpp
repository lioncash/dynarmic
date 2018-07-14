/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>
#include <functional>
#include <type_traits>

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

template <typename Function>
static void EmitAVXVectorOperation(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

    (code.*fn)(xmm_a, xmm_a, xmm_b);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

template <typename Lambda>
static void EmitOneArgumentFallback(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    const auto fn = static_cast<mp::equivalent_function_type_t<Lambda>*>(lambda);
    constexpr u32 stack_space = 2 * 16;
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm arg1 = ctx.reg_alloc.UseXmm(args[0]);
    ctx.reg_alloc.EndOfAllocScope();

    ctx.reg_alloc.HostCall(nullptr);
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);

    code.movaps(xword[code.ABI_PARAM2], arg1);
    code.CallFunction(fn);
    code.movaps(xmm0, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);

    ctx.reg_alloc.DefineValue(inst, xmm0);
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
    code.CallFunction(fn);
    code.movaps(xmm0, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);

    ctx.reg_alloc.DefineValue(inst, xmm0);
}

void EmitX64::EmitVectorGetElement8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    const u8 index = args[1].GetImmediateU8();

    const Xbyak::Xmm source = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Reg32 dest = ctx.reg_alloc.ScratchGpr().cvt32();

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.pextrb(dest, source, index);
    } else {
        code.pextrw(dest, source, index / 2);
        if (index % 2 == 1) {
            code.shr(dest, 8);
        }
    }

    ctx.reg_alloc.DefineValue(inst, dest);
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
    const u8 index = args[1].GetImmediateU8();
    const Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        const Xbyak::Reg8 source_elem = ctx.reg_alloc.UseGpr(args[2]).cvt8();

        code.pinsrb(source_vector, source_elem.cvt32(), index);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    } else {
        const Xbyak::Reg32 source_elem = ctx.reg_alloc.UseScratchGpr(args[2]).cvt32();
        const Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr().cvt32();

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
    const u8 index = args[1].GetImmediateU8();
    const Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        const Xbyak::Reg32 source_elem = ctx.reg_alloc.UseGpr(args[2]).cvt32();

        code.pinsrd(source_vector, source_elem, index);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    } else {
        const Xbyak::Reg32 source_elem = ctx.reg_alloc.UseScratchGpr(args[2]).cvt32();

        code.pinsrw(source_vector, source_elem, index * 2);
        code.shr(source_elem, 16);
        code.pinsrw(source_vector, source_elem, index * 2 + 1);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    }
}

void EmitX64::EmitVectorSetElement64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    const u8 index = args[1].GetImmediateU8();
    const Xbyak::Xmm source_vector = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        const Xbyak::Reg64 source_elem = ctx.reg_alloc.UseGpr(args[2]);

        code.pinsrq(source_vector, source_elem, index);

        ctx.reg_alloc.DefineValue(inst, source_vector);
    } else {
        const Xbyak::Reg64 source_elem = ctx.reg_alloc.UseScratchGpr(args[2]);

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

static void EmitVectorAbs(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm data = ctx.reg_alloc.UseScratchXmm(args[0]);

    switch (esize) {
    case 8:
        if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
            code.pabsb(data, data);
        } else {
            const Xbyak::Xmm temp = ctx.reg_alloc.ScratchXmm();
            code.pxor(temp, temp);
            code.psubb(temp, data);
            code.pminub(data, temp);
        }
        break;
    case 16:
        if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
            code.pabsw(data, data);
        } else {
            const Xbyak::Xmm temp = ctx.reg_alloc.ScratchXmm();
            code.pxor(temp, temp);
            code.psubw(temp, data);
            code.pmaxsw(data, temp);
        }
        break;
    case 32:
        if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
            code.pabsd(data, data);
        } else {
            const Xbyak::Xmm temp = ctx.reg_alloc.ScratchXmm();
            code.movdqa(temp, data);
            code.psrad(temp, 31);
            code.pxor(data, temp);
            code.psubd(data, temp);
        }
        break;
    case 64:
        if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
            code.vpabsq(data, data);
        } else {
            const Xbyak::Xmm temp = ctx.reg_alloc.ScratchXmm();
            code.pshufd(temp, data, 0b11110101);
            code.psrad(temp, 31);
            code.pxor(data, temp);
            code.psubq(data, temp);
        }
        break;
    }

    ctx.reg_alloc.DefineValue(inst, data);
}

void EmitX64::EmitVectorAbs8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorAbs(8, ctx, inst, code);
}

void EmitX64::EmitVectorAbs16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorAbs(16, ctx, inst, code);
}

void EmitX64::EmitVectorAbs32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorAbs(32, ctx, inst, code);
}

void EmitX64::EmitVectorAbs64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorAbs(64, ctx, inst, code);
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

static void ArithmeticShiftRightByte(EmitContext& ctx, BlockOfCode& code, const Xbyak::Xmm& result, u8 shift_amount) {
    const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    // TODO: Optimize
    code.movdqa(tmp, result);
    code.pslldq(tmp, 1);
    code.psraw(tmp, shift_amount);
    code.psraw(result, shift_amount + 8);
    code.psllw(result, 8);
    code.psrlw(tmp, 8);
    code.por(result, tmp);
}

void EmitX64::EmitVectorArithmeticShiftRight8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = args[1].GetImmediateU8();

    ArithmeticShiftRightByte(ctx, code, result, shift_amount);

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
    const Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    const u8 shift_amount = std::min(args[1].GetImmediateU8(), u8(63));

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        code.vpsraq(result, result, shift_amount);
    } else {
        const Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm tmp2 = ctx.reg_alloc.ScratchXmm();

        const u64 sign_bit = 0x80000000'00000000u >> shift_amount;

        code.pxor(tmp2, tmp2);
        code.psrlq(result, shift_amount);
        code.movdqa(tmp1, code.MConst(xword, sign_bit, sign_bit));
        code.pand(tmp1, result);
        code.psubq(tmp2, tmp1);
        code.por(result, tmp2);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorBroadcastLower8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX2)) {
        code.vpbroadcastb(a, a);
        code.movq(a, a);
    } else if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
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

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX2)) {
        code.vpbroadcastb(a, a);
    } else if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
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

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX2)) {
        code.vpbroadcastw(a, a);
    } else {
        code.pshuflw(a, a, 0);
        code.punpcklqdq(a, a);
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX2)) {
        code.vpbroadcastd(a, a);
    } else {
        code.pshufd(a, a, 0);
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorBroadcast64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX2)) {
        code.vpbroadcastq(a, a);
    } else {
        code.punpcklqdq(a, a);
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorDeinterleaveEven8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);
    const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.movdqa(tmp, code.MConst(xword, 0x00FF00FF00FF00FF, 0x00FF00FF00FF00FF));
    code.pand(lhs, tmp);
    code.pand(rhs, tmp);
    code.packuswb(lhs, rhs);

    ctx.reg_alloc.DefineValue(inst, lhs);
}

void EmitX64::EmitVectorDeinterleaveEven16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.pslld(lhs, 16);
    code.psrad(lhs, 16);

    code.pslld(rhs, 16);
    code.psrad(rhs, 16);

    code.packssdw(lhs, rhs);

    ctx.reg_alloc.DefineValue(inst, lhs);
}

void EmitX64::EmitVectorDeinterleaveEven32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.pshufd(lhs, lhs, 0b10001000);
    code.pshufd(rhs, rhs, 0b10001000);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.pblendw(lhs, rhs, 0b11110000);
    } else {
        code.punpcklqdq(lhs, rhs);
    }

    ctx.reg_alloc.DefineValue(inst, lhs);
}

void EmitX64::EmitVectorDeinterleaveEven64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.movq(lhs, lhs);
    code.pslldq(rhs, 8);
    code.por(lhs, rhs);

    ctx.reg_alloc.DefineValue(inst, lhs);
}

void EmitX64::EmitVectorDeinterleaveOdd8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.psraw(lhs, 8);
    code.psraw(rhs, 8);
    code.packsswb(lhs, rhs);

    ctx.reg_alloc.DefineValue(inst, lhs);
}

void EmitX64::EmitVectorDeinterleaveOdd16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.psrad(lhs, 16);
    code.psrad(rhs, 16);
    code.packssdw(lhs, rhs);

    ctx.reg_alloc.DefineValue(inst, lhs);
}

void EmitX64::EmitVectorDeinterleaveOdd32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.pshufd(lhs, lhs, 0b11011101);
    code.pshufd(rhs, rhs, 0b11011101);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.pblendw(lhs, rhs, 0b11110000);
    } else {
        code.punpcklqdq(lhs, rhs);
    }

    ctx.reg_alloc.DefineValue(inst, lhs);
}

void EmitX64::EmitVectorDeinterleaveOdd64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm lhs = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm rhs = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.punpckhqdq(lhs, rhs);

    ctx.reg_alloc.DefineValue(inst, lhs);
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

void EmitX64::EmitVectorExtract(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm xmm_b = ctx.reg_alloc.UseScratchXmm(args[1]);

    const u8 position = args[2].GetImmediateU8();
    ASSERT(position % 8 == 0);

    code.psrldq(xmm_a, position / 8);
    code.pslldq(xmm_b, (128 - position) / 8);
    code.por(xmm_a, xmm_b);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorExtractLower(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm xmm_b = ctx.reg_alloc.UseScratchXmm(args[1]);

    const u8 position = args[2].GetImmediateU8();
    ASSERT(position % 8 == 0);

    code.psrldq(xmm_a, position / 8);
    code.pslldq(xmm_b, (64 - position) / 8);
    code.por(xmm_a, xmm_b);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitVectorGreaterS8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpgtb);
}

void EmitX64::EmitVectorGreaterS16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpgtw);
}

void EmitX64::EmitVectorGreaterS32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpgtd);
}

void EmitX64::EmitVectorGreaterS64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE42)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pcmpgtq);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u64, 2>& result, const std::array<s64, 2>& a, const std::array<s64, 2>& b){
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = (a[i] > b[i]) ? ~u64(0) : 0;
        }
    });
}

static void EmitVectorHalvingAddSigned(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
    const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.movdqa(tmp, b);
    code.pand(tmp, a);
    code.pxor(a, b);

    switch (esize) {
    case 8:
        ArithmeticShiftRightByte(ctx, code, a, 1);
        code.paddb(a, tmp);
        break;
    case 16:
        code.psraw(a, 1);
        code.paddw(a, tmp);
        break;
    case 32:
        code.psrad(a, 1);
        code.paddd(a, tmp);
        break;
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorHalvingAddS8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingAddSigned(8, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingAddS16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingAddSigned(16, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingAddS32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingAddSigned(32, ctx, inst, code);
}

static void EmitVectorHalvingAddUnsigned(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
    const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.movdqa(tmp, b);

    switch (esize) {
    case 8:
        code.pavgb(tmp, a);
        code.pxor(a, b);
        code.pand(a, code.MConst(xword, 0x0101010101010101, 0x0101010101010101));
        code.psubb(tmp, a);
        break;
    case 16:
        code.pavgw(tmp, a);
        code.pxor(a, b);
        code.pand(a, code.MConst(xword, 0x0001000100010001, 0x0001000100010001));
        code.psubw(tmp, a);
        break;
    case 32:
        code.pand(tmp, a);
        code.pxor(a, b);
        code.psrld(a, 1);
        code.paddd(tmp, a);
        break;
    }

    ctx.reg_alloc.DefineValue(inst, tmp);
}

void EmitX64::EmitVectorHalvingAddU8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingAddUnsigned(8, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingAddU16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingAddUnsigned(16, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingAddU32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingAddUnsigned(32, ctx, inst, code);
}

static void EmitVectorHalvingSubSigned(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    switch (esize) {
    case 8: {
        const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
        code.movdqa(tmp, code.MConst(xword, 0x8080808080808080, 0x8080808080808080));
        code.pxor(a, tmp);
        code.pxor(b, tmp);
        code.pavgb(b, a);
        code.psubb(a, b);
        break;
    }
    case 16: {
        const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
        code.movdqa(tmp, code.MConst(xword, 0x8000800080008000, 0x8000800080008000));
        code.pxor(a, tmp);
        code.pxor(b, tmp);
        code.pavgw(b, a);
        code.psubw(a, b);
        break;
    }
    case 32:
        code.pxor(a, b);
        code.pand(b, a);
        code.psrad(a, 1);
        code.psubd(a, b);
        break;
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorHalvingSubS8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingSubSigned(8, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingSubS16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingSubSigned(16, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingSubS32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingSubSigned(32, ctx, inst, code);
}

static void EmitVectorHalvingSubUnsigned(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    switch (esize) {
    case 8:
        code.pavgb(b, a);
        code.psubb(a, b);
        break;
    case 16:
        code.pavgw(b, a);
        code.psubw(a, b);
        break;
    case 32:
        code.pxor(a, b);
        code.pand(b, a);
        code.psrld(a, 1);
        code.psubd(a, b);
        break;
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorHalvingSubU8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingSubUnsigned(8, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingSubU16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingSubUnsigned(16, ctx, inst, code);
}

void EmitX64::EmitVectorHalvingSubU32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorHalvingSubUnsigned(32, ctx, inst, code);
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

static void EmitVectorInterleaveUpper(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, int size) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    switch (size) {
    case 8:
        code.punpckhbw(a, b);
        break;
    case 16:
        code.punpckhwd(a, b);
        break;
    case 32:
        code.punpckhdq(a, b);
        break;
    case 64:
        code.punpckhqdq(a, b);
        break;
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorInterleaveUpper8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveUpper(code, ctx, inst, 8);
}

void EmitX64::EmitVectorInterleaveUpper16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveUpper(code, ctx, inst, 16);
}

void EmitX64::EmitVectorInterleaveUpper32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveUpper(code, ctx, inst, 32);
}

void EmitX64::EmitVectorInterleaveUpper64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorInterleaveUpper(code, ctx, inst, 64);
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

template <typename T>
static constexpr T LogicalVShift(T x, T y) {
    const s8 shift_amount = static_cast<s8>(static_cast<u8>(y));
    const s64 bit_size = static_cast<s64>(Common::BitSize<T>());

    if constexpr (std::is_signed_v<T>) {
        if (shift_amount >= bit_size) {
            return 0;
        }

        if (shift_amount <= -bit_size) {
            // Parentheses necessary, as MSVC doesn't appear to consider cast parentheses
            // as a grouping in terms of precedence, causing warning C4554 to fire. See:
            // https://developercommunity.visualstudio.com/content/problem/144783/msvc-2017-does-not-understand-that-static-cast-cou.html
            return x >> (T(bit_size - 1));
        }
    } else if (shift_amount <= -bit_size || shift_amount >= bit_size) {
        return 0;
    }

    if (shift_amount < 0) {
        return x >> T(-shift_amount);
    }

    using unsigned_type = std::make_unsigned_t<T>;
    return static_cast<T>(static_cast<unsigned_type>(x) << static_cast<unsigned_type>(shift_amount));
}

void EmitX64::EmitVectorLogicalVShiftS8(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<s8, 16>& result, const std::array<s8, 16>& a, const std::array<s8, 16>& b) {
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<s8>);
    });
}

void EmitX64::EmitVectorLogicalVShiftS16(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<s16, 8>& result, const std::array<s16, 8>& a, const std::array<s16, 8>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<s16>);
    });
}

void EmitX64::EmitVectorLogicalVShiftS32(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<s32, 4>& result, const std::array<s32, 4>& a, const std::array<s32, 4>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<s32>);
    });
}

void EmitX64::EmitVectorLogicalVShiftS64(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<s64, 2>& result, const std::array<s64, 2>& a, const std::array<s64, 2>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<s64>);
    });
}

void EmitX64::EmitVectorLogicalVShiftU8(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u8, 16>& result, const std::array<u8, 16>& a, const std::array<u8, 16>& b) {
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<u8>);
    });
}

void EmitX64::EmitVectorLogicalVShiftU16(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u16, 8>& result, const std::array<u16, 8>& a, const std::array<u16, 8>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<u16>);
    });
}

void EmitX64::EmitVectorLogicalVShiftU32(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u32, 4>& result, const std::array<u32, 4>& a, const std::array<u32, 4>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<u32>);
    });
}

void EmitX64::EmitVectorLogicalVShiftU64(EmitContext& ctx, IR::Inst* inst) {
    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u64, 2>& result, const std::array<u64, 2>& a, const std::array<u64, 2>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), LogicalVShift<u64>);
    });
}

void EmitX64::EmitVectorMaxS8(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmaxsb);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    const Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_b, b);

    code.pcmpgtb(tmp_b, a);
    code.pand(b, tmp_b);
    code.pandn(tmp_b, a);
    code.por(tmp_b, b);

    ctx.reg_alloc.DefineValue(inst, tmp_b);
}

void EmitX64::EmitVectorMaxS16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmaxsw);
}

void EmitX64::EmitVectorMaxS32(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmaxsd);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    const Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_b, b);

    code.pcmpgtd(tmp_b, a);
    code.pand(b, tmp_b);
    code.pandn(tmp_b, a);
    code.por(tmp_b, b);

    ctx.reg_alloc.DefineValue(inst, tmp_b);
}

void EmitX64::EmitVectorMaxS64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        EmitAVXVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::vpmaxsq);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<s64, 2>& result, const std::array<s64, 2>& a, const std::array<s64, 2>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), [](auto x, auto y) { return std::max(x, y); });
    });
}

void EmitX64::EmitVectorMaxU8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmaxub);
}

void EmitX64::EmitVectorMaxU16(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmaxuw);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    code.psubusw(a, b);
    code.paddw(a, b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorMaxU32(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pmaxud);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp, code.MConst(xword, 0x8000000080000000, 0x8000000080000000));

    const Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_b, b);

    code.pxor(tmp_b, tmp);
    code.pxor(tmp, a);

    code.pcmpgtd(tmp, tmp_b);
    code.pand(a, tmp);
    code.pandn(tmp, b);
    code.por(a, tmp);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorMaxU64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        EmitAVXVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::vpmaxuq);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u64, 2>& result, const std::array<u64, 2>& a, const std::array<u64, 2>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), [](auto x, auto y) { return std::max(x, y); });
    });
}

void EmitX64::EmitVectorMinS8(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pminsb);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    const Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_b, b);

    code.pcmpgtb(tmp_b, a);
    code.pand(a, tmp_b);
    code.pandn(tmp_b, b);
    code.por(a, tmp_b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorMinS16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pminsw);
}

void EmitX64::EmitVectorMinS32(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pminsd);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    const Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_b, b);

    code.pcmpgtd(tmp_b, a);
    code.pand(a, tmp_b);
    code.pandn(tmp_b, b);
    code.por(a, tmp_b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorMinS64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        EmitAVXVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::vpminsq);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<s64, 2>& result, const std::array<s64, 2>& a, const std::array<s64, 2>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), [](auto x, auto y) { return std::min(x, y); });
    });
}

void EmitX64::EmitVectorMinU8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pminub);
}

void EmitX64::EmitVectorMinU16(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pminuw);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    const Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_b, b);

    code.psubusw(tmp_b, a);
    code.psubw(b, tmp_b);

    ctx.reg_alloc.DefineValue(inst, b);
}

void EmitX64::EmitVectorMinU32(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pminud);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    const Xbyak::Xmm sint_max_plus_one = ctx.reg_alloc.ScratchXmm();
    code.movdqa(sint_max_plus_one, code.MConst(xword, 0x8000000080000000, 0x8000000080000000));

    const Xbyak::Xmm tmp_a = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_a, a);
    code.psubd(tmp_a, sint_max_plus_one);

    const Xbyak::Xmm tmp_b = ctx.reg_alloc.ScratchXmm();
    code.movdqa(tmp_b, b);
    code.psubd(tmp_b, sint_max_plus_one);

    code.pcmpgtd(tmp_b, tmp_a);
    code.pand(a, tmp_b);
    code.pandn(tmp_b, b);
    code.por(a, tmp_b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorMinU64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        EmitAVXVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::vpminuq);
        return;
    }

    EmitTwoArgumentFallback(code, ctx, inst, [](std::array<u64, 2>& result, const std::array<u64, 2>& a, const std::array<u64, 2>& b){
        std::transform(a.begin(), a.end(), b.begin(), result.begin(), [](auto x, auto y) { return std::min(x, y); });
    });
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
    code.pand(a, code.MConst(xword, 0x00FF00FF00FF00FF, 0x00FF00FF00FF00FF));
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

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
    const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

    code.movdqa(tmp, a);
    code.psrlq(a, 32);
    code.pmuludq(tmp, b);
    code.psrlq(b, 32);
    code.pmuludq(a, b);
    code.pshufd(tmp, tmp, 0b00001000);
    code.pshufd(b, a, 0b00001000);
    code.punpckldq(tmp, b);

    ctx.reg_alloc.DefineValue(inst, tmp);
}

void EmitX64::EmitVectorMultiply64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512DQ) && code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        EmitAVXVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::vpmullq);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
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

    const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
    const Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();
    const Xbyak::Xmm tmp2 = ctx.reg_alloc.ScratchXmm();
    const Xbyak::Xmm tmp3 = ctx.reg_alloc.ScratchXmm();

    code.movdqa(tmp1, a);
    code.movdqa(tmp2, a);
    code.movdqa(tmp3, b);

    code.psrlq(tmp1, 32);
    code.psrlq(tmp3, 32);

    code.pmuludq(tmp2, b);
    code.pmuludq(tmp3, a);
    code.pmuludq(b, tmp1);

    code.paddq(b, tmp3);
    code.psllq(b, 32);
    code.paddq(tmp2, b);

    ctx.reg_alloc.DefineValue(inst, tmp2);
}

void EmitX64::EmitVectorNarrow16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();

    // TODO: AVX512F implementation

    code.pxor(zeros, zeros);
    code.pand(a, code.MConst(xword, 0x00FF00FF00FF00FF, 0x00FF00FF00FF00FF));
    code.packuswb(a, zeros);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorNarrow32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm zeros = ctx.reg_alloc.ScratchXmm();

    // TODO: AVX512F implementation

    code.pxor(zeros, zeros);
    code.pand(a, code.MConst(xword, 0x0000FFFF0000FFFF, 0x0000FFFF0000FFFF));
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
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512_BITALG)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        const Xbyak::Xmm data = ctx.reg_alloc.UseScratchXmm(args[0]);

        code.vpopcntb(data, data);

        ctx.reg_alloc.DefineValue(inst, data);
        return;
    }

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        Xbyak::Xmm low_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm high_a = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();
        Xbyak::Xmm tmp2 = ctx.reg_alloc.ScratchXmm();

        code.movdqa(high_a, low_a);
        code.psrlw(high_a, 4);
        code.movdqa(tmp1, code.MConst(xword, 0x0F0F0F0F0F0F0F0F, 0x0F0F0F0F0F0F0F0F));
        code.pand(high_a, tmp1); // High nibbles
        code.pand(low_a, tmp1); // Low nibbles

        code.movdqa(tmp1, code.MConst(xword, 0x0302020102010100, 0x0403030203020201));
        code.movdqa(tmp2, tmp1);
        code.pshufb(tmp1, low_a);
        code.pshufb(tmp2, high_a);

        code.paddb(tmp1, tmp2);

        ctx.reg_alloc.DefineValue(inst, tmp1);
        return;
    }

    EmitOneArgumentFallback(code, ctx, inst, [](std::array<u8, 16>& result, const std::array<u8, 16>& a){
        std::transform(a.begin(), a.end(), result.begin(), [](u8 val) {
            return static_cast<u8>(Common::BitCount(val));
        });
    });
}

void EmitX64::EmitVectorReverseBits(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm data = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm high_nibble_reg = ctx.reg_alloc.ScratchXmm();

    code.movdqa(high_nibble_reg, code.MConst(xword, 0xF0F0F0F0F0F0F0F0, 0xF0F0F0F0F0F0F0F0));
    code.pand(high_nibble_reg, data);
    code.pxor(data, high_nibble_reg);
    code.psrld(high_nibble_reg, 4);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSSE3)) {
        // High lookup
        const Xbyak::Xmm high_reversed_reg = ctx.reg_alloc.ScratchXmm();
        code.movdqa(high_reversed_reg, code.MConst(xword, 0xE060A020C0408000, 0xF070B030D0509010));
        code.pshufb(high_reversed_reg, data);

        // Low lookup (low nibble equivalent of the above)
        code.movdqa(data, code.MConst(xword, 0x0E060A020C040800, 0x0F070B030D050901));
        code.pshufb(data, high_nibble_reg);
        code.por(data, high_reversed_reg);
    } else {
        code.pslld(data, 4);
        code.por(data, high_nibble_reg);

        code.movdqa(high_nibble_reg, code.MConst(xword, 0xCCCCCCCCCCCCCCCC, 0xCCCCCCCCCCCCCCCC));
        code.pand(high_nibble_reg, data);
        code.pxor(data, high_nibble_reg);
        code.psrld(high_nibble_reg, 2);
        code.pslld(data, 2);
        code.por(data, high_nibble_reg);

        code.movdqa(high_nibble_reg, code.MConst(xword, 0xAAAAAAAAAAAAAAAA, 0xAAAAAAAAAAAAAAAA));
        code.pand(high_nibble_reg, data);
        code.pxor(data, high_nibble_reg);
        code.psrld(high_nibble_reg, 1);
        code.paddd(data, data);
        code.por(data, high_nibble_reg);
    }

    ctx.reg_alloc.DefineValue(inst, data);
}

static void EmitVectorRoundingHalvingAddSigned(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    switch (esize) {
    case 8: {
        const Xbyak::Xmm vec_128 = ctx.reg_alloc.ScratchXmm();
        code.movdqa(vec_128, code.MConst(xword, 0x8080808080808080, 0x8080808080808080));

        code.paddb(a, vec_128);
        code.paddb(b, vec_128);
        code.pavgb(a, b);
        code.paddb(a, vec_128);
        break;
    }
    case 16: {
        const Xbyak::Xmm vec_32768 = ctx.reg_alloc.ScratchXmm();
        code.movdqa(vec_32768, code.MConst(xword, 0x8000800080008000, 0x8000800080008000));
        
        code.paddw(a, vec_32768);
        code.paddw(b, vec_32768);
        code.pavgw(a, b);
        code.paddw(a, vec_32768);
        break;
    }
    case 32: {
        const Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();
        code.movdqa(tmp1, a);

        code.por(a, b);
        code.psrad(tmp1, 1);
        code.psrad(b, 1);
        code.pslld(a, 31);
        code.paddd(b, tmp1);
        code.psrld(a, 31);
        code.paddd(a, b);
        break;
    }
    }

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitVectorRoundingHalvingAddS8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorRoundingHalvingAddSigned(8, ctx, inst, code);
}

void EmitX64::EmitVectorRoundingHalvingAddS16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorRoundingHalvingAddSigned(16, ctx, inst, code);
}

void EmitX64::EmitVectorRoundingHalvingAddS32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorRoundingHalvingAddSigned(32, ctx, inst, code);
}

static void EmitVectorRoundingHalvingAddUnsigned(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    switch (esize) {
    case 8:
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pavgb);
        return;
    case 16:
        EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pavgw);
        return;
    case 32: {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);
        const Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();

        code.movdqa(tmp1, a);

        code.por(a, b);
        code.psrld(tmp1, 1);
        code.psrld(b, 1);
        code.pslld(a, 31);
        code.paddd(b, tmp1);
        code.psrld(a, 31);
        code.paddd(a, b);

        ctx.reg_alloc.DefineValue(inst, a);
        break;
    }
    }
}

void EmitX64::EmitVectorRoundingHalvingAddU8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorRoundingHalvingAddUnsigned(8, ctx, inst, code);
}

void EmitX64::EmitVectorRoundingHalvingAddU16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorRoundingHalvingAddUnsigned(16, ctx, inst, code);
}

void EmitX64::EmitVectorRoundingHalvingAddU32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorRoundingHalvingAddUnsigned(32, ctx, inst, code);
}

enum class ShuffleType {
    LowHalfwords,
    HighHalfwords,
    Words
};

static void VectorShuffleImpl(ShuffleType type, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm operand = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    const u8 mask = args[1].GetImmediateU8();

    if (type == ShuffleType::LowHalfwords) {
        code.pshuflw(result, operand, mask);
    } else if (type == ShuffleType::HighHalfwords) {
        code.pshufhw(result, operand, mask);
    } else {
        code.pshufd(result, operand, mask);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitVectorShuffleHighHalfwords(EmitContext& ctx, IR::Inst* inst) {
    VectorShuffleImpl(ShuffleType::HighHalfwords, ctx, inst, code);
}

void EmitX64::EmitVectorShuffleLowHalfwords(EmitContext& ctx, IR::Inst* inst) {
    VectorShuffleImpl(ShuffleType::LowHalfwords, ctx, inst, code);
}

void EmitX64::EmitVectorShuffleWords(EmitContext& ctx, IR::Inst* inst) {
    VectorShuffleImpl(ShuffleType::Words, ctx, inst, code);
}

void EmitX64::EmitVectorSignExtend8(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        code.pmovsxbw(a, a);
        ctx.reg_alloc.DefineValue(inst, a);
    } else {
        const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
        const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
        code.pxor(result, result);
        code.punpcklbw(result, a);
        code.psraw(result, 8);
        ctx.reg_alloc.DefineValue(inst, result);
    }
}

void EmitX64::EmitVectorSignExtend16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        ctx.reg_alloc.DefineValue(inst, a);
        code.pmovsxwd(a, a);
    } else {
        const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
        const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
        code.pxor(result, result);
        code.punpcklwd(result, a);
        code.psrad(result, 16);
        ctx.reg_alloc.DefineValue(inst, result);
    }
}

void EmitX64::EmitVectorSignExtend32(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
        code.pmovsxdq(a, a);
        ctx.reg_alloc.DefineValue(inst, a);
        return;
    }

    EmitOneArgumentFallback(code, ctx, inst, [](std::array<u64, 2>& result, const std::array<u32, 4>& a){
        for (size_t i = 0; i < result.size(); ++i) {
            result[i] = Common::SignExtend<32, u64>(a[i]);
        }
    });
}

void EmitX64::EmitVectorSignExtend64(EmitContext& ctx, IR::Inst* inst) {
    EmitOneArgumentFallback(code, ctx, inst, [](std::array<u64, 2>& result, const std::array<u64, 2>& a){
        result[1] = (a[0] >> 63) ? ~u64(0) : 0;
        result[0] = a[0];
    });
}

static void EmitVectorSignedAbsoluteDifference(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm x = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm y = ctx.reg_alloc.UseXmm(args[1]);
    const Xbyak::Xmm mask = ctx.reg_alloc.ScratchXmm();
    const Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();
    const Xbyak::Xmm tmp2 = ctx.reg_alloc.ScratchXmm();

    code.movdqa(mask, x);
    code.movdqa(tmp1, y);

    switch (esize) {
    case 8:
        code.pcmpgtb(mask, y);
        code.psubb(tmp1, x);
        code.psubb(x, y);
        break;
    case 16:
        code.pcmpgtw(mask, y);
        code.psubw(tmp1, x);
        code.psubw(x, y);
        break;
    case 32:
        code.pcmpgtd(mask, y);
        code.psubd(tmp1, x);
        code.psubd(x, y);
        break;
    }

    code.movdqa(tmp2, mask);
    code.pand(x, mask);
    code.pandn(tmp2, tmp1);
    code.por(x, tmp2);

    ctx.reg_alloc.DefineValue(inst, x);
}

void EmitX64::EmitVectorSignedAbsoluteDifference8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorSignedAbsoluteDifference(8, ctx, inst, code);
}

void EmitX64::EmitVectorSignedAbsoluteDifference16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorSignedAbsoluteDifference(16, ctx, inst, code);
}

void EmitX64::EmitVectorSignedAbsoluteDifference32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorSignedAbsoluteDifference(32, ctx, inst, code);
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

static void EmitVectorUnsignedAbsoluteDifference(size_t esize, EmitContext& ctx, IR::Inst* inst, BlockOfCode& code) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm temp = ctx.reg_alloc.ScratchXmm();
    const Xbyak::Xmm x = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm y = ctx.reg_alloc.UseScratchXmm(args[1]);

    switch (esize) {
    case 8:
        code.movdqa(temp, x);
        code.psubusb(temp, y);
        code.psubusb(y, x);
        code.por(temp, y);
        break;
    case 16:
        code.movdqa(temp, x);
        code.psubusw(temp, y);
        code.psubusw(y, x);
        code.por(temp, y);
        break;
    case 32:
        if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
            code.movdqa(temp, x);
            code.pminud(x, y);
            code.pmaxud(temp, y);
            code.psubd(temp, x);
        } else {
            code.movdqa(temp, code.MConst(xword, 0x8000000080000000, 0x8000000080000000));
            code.pxor(x, temp);
            code.pxor(y, temp);
            code.movdqa(temp, x);
            code.psubd(temp, y);
            code.pcmpgtd(y, x);
            code.psrld(y, 1);
            code.pxor(temp, y);
            code.psubd(temp, y);
        }
        break;
    }

    ctx.reg_alloc.DefineValue(inst, temp);
}

void EmitX64::EmitVectorUnsignedAbsoluteDifference8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorUnsignedAbsoluteDifference(8, ctx, inst, code);
}

void EmitX64::EmitVectorUnsignedAbsoluteDifference16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorUnsignedAbsoluteDifference(16, ctx, inst, code);
}

void EmitX64::EmitVectorUnsignedAbsoluteDifference32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorUnsignedAbsoluteDifference(32, ctx, inst, code);
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

void EmitX64::EmitZeroVector(EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Xmm a = ctx.reg_alloc.ScratchXmm();
    code.pxor(a, a);
    ctx.reg_alloc.DefineValue(inst, a);
}

} // namespace Dynarmic::BackendX64
