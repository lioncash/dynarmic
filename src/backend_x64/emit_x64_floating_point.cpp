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

constexpr u64 f32_negative_zero = 0x80000000u;
constexpr u64 f32_nan = 0x7fc00000u;
constexpr u64 f32_non_sign_mask = 0x7fffffffu;

constexpr u64 f64_negative_zero = 0x8000000000000000u;
constexpr u64 f64_nan = 0x7ff8000000000000u;
constexpr u64 f64_non_sign_mask = 0x7fffffffffffffffu;

constexpr u64 f64_penultimate_positive_denormal = 0x000ffffffffffffeu;
constexpr u64 f64_min_s32 = 0xc1e0000000000000u; // -2147483648 as a double
constexpr u64 f64_max_s32 = 0x41dfffffffc00000u; // 2147483647 as a double
constexpr u64 f64_min_u32 = 0x0000000000000000u; // 0 as a double
constexpr u64 f64_max_u32 = 0x41efffffffe00000u; // 4294967295 as a double

static void DenormalsAreZero32(BlockOfCode& code, Xbyak::Xmm xmm_value, Xbyak::Reg32 gpr_scratch) {
    Xbyak::Label end;

    // We need to report back whether we've found a denormal on input.
    // SSE doesn't do this for us when SSE's DAZ is enabled.

    code.movd(gpr_scratch, xmm_value);
    code.and_(gpr_scratch, u32(0x7FFFFFFF));
    code.sub(gpr_scratch, u32(1));
    code.cmp(gpr_scratch, u32(0x007FFFFE));
    code.ja(end);
    code.pxor(xmm_value, xmm_value);
    code.mov(dword[r15 + code.GetJitStateInfo().offsetof_FPSCR_IDC], u32(1 << 7));
    code.L(end);
}

static void DenormalsAreZero64(BlockOfCode& code, Xbyak::Xmm xmm_value, Xbyak::Reg64 gpr_scratch) {
    Xbyak::Label end;

    auto mask = code.MConst(f64_non_sign_mask);
    mask.setBit(64);
    auto penult_denormal = code.MConst(f64_penultimate_positive_denormal);
    penult_denormal.setBit(64);

    code.movq(gpr_scratch, xmm_value);
    code.and_(gpr_scratch, mask);
    code.sub(gpr_scratch, u32(1));
    code.cmp(gpr_scratch, penult_denormal);
    code.ja(end);
    code.pxor(xmm_value, xmm_value);
    code.mov(dword[r15 + code.GetJitStateInfo().offsetof_FPSCR_IDC], u32(1 << 7));
    code.L(end);
}

static void FlushToZero32(BlockOfCode& code, Xbyak::Xmm xmm_value, Xbyak::Reg32 gpr_scratch) {
    Xbyak::Label end;

    code.movd(gpr_scratch, xmm_value);
    code.and_(gpr_scratch, u32(0x7FFFFFFF));
    code.sub(gpr_scratch, u32(1));
    code.cmp(gpr_scratch, u32(0x007FFFFE));
    code.ja(end);
    code.pxor(xmm_value, xmm_value);
    code.mov(dword[r15 + code.GetJitStateInfo().offsetof_FPSCR_UFC], u32(1 << 3));
    code.L(end);
}

static void FlushToZero64(BlockOfCode& code, Xbyak::Xmm xmm_value, Xbyak::Reg64 gpr_scratch) {
    Xbyak::Label end;

    auto mask = code.MConst(f64_non_sign_mask);
    mask.setBit(64);
    auto penult_denormal = code.MConst(f64_penultimate_positive_denormal);
    penult_denormal.setBit(64);

    code.movq(gpr_scratch, xmm_value);
    code.and_(gpr_scratch, mask);
    code.sub(gpr_scratch, u32(1));
    code.cmp(gpr_scratch, penult_denormal);
    code.ja(end);
    code.pxor(xmm_value, xmm_value);
    code.mov(dword[r15 + code.GetJitStateInfo().offsetof_FPSCR_UFC], u32(1 << 3));
    code.L(end);
}

static void DefaultNaN32(BlockOfCode& code, Xbyak::Xmm xmm_value) {
    Xbyak::Label end;

    code.ucomiss(xmm_value, xmm_value);
    code.jnp(end);
    code.movaps(xmm_value, code.MConst(f32_nan));
    code.L(end);
}

static void DefaultNaN64(BlockOfCode& code, Xbyak::Xmm xmm_value) {
    Xbyak::Label end;

    code.ucomisd(xmm_value, xmm_value);
    code.jnp(end);
    code.movaps(xmm_value, code.MConst(f64_nan));
    code.L(end);
}

static void ZeroIfNaN64(BlockOfCode& code, Xbyak::Xmm xmm_value, Xbyak::Xmm xmm_scratch) {
    code.pxor(xmm_scratch, xmm_scratch);
    code.cmpordsd(xmm_scratch, xmm_value); // true mask when ordered (i.e.: when not an NaN)
    code.pand(xmm_value, xmm_scratch);
}

static void FPThreeOp32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Reg32 gpr_scratch = ctx.reg_alloc.ScratchGpr().cvt32();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
        DenormalsAreZero32(code, operand, gpr_scratch);
    }
    (code.*fn)(result, operand);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN32(code, result);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

static void FPThreeOp64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Reg64 gpr_scratch = ctx.reg_alloc.ScratchGpr();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
        DenormalsAreZero64(code, operand, gpr_scratch);
    }
    (code.*fn)(result, operand);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN64(code, result);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

static void FPTwoOp32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 gpr_scratch = ctx.reg_alloc.ScratchGpr().cvt32();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
    }

    (code.*fn)(result, result);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN32(code, result);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

static void FPTwoOp64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = ctx.reg_alloc.ScratchGpr();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
    }

    (code.*fn)(result, result);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN64(code, result);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPAbs32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pand(result, code.MConst(f32_non_sign_mask));

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPAbs64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pand(result, code.MConst(f64_non_sign_mask));

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPNeg32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pxor(result, code.MConst(f32_negative_zero));

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPNeg64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pxor(result, code.MConst(f64_negative_zero));

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPAdd32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, &Xbyak::CodeGenerator::addss);
}

void EmitX64::EmitFPAdd64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, &Xbyak::CodeGenerator::addsd);
}

void EmitX64::EmitFPDiv32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, &Xbyak::CodeGenerator::divss);
}

void EmitX64::EmitFPDiv64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, &Xbyak::CodeGenerator::divsd);
}

void EmitX64::EmitFPMul32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, &Xbyak::CodeGenerator::mulss);
}

void EmitX64::EmitFPMul64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, &Xbyak::CodeGenerator::mulsd);
}

void EmitX64::EmitFPSqrt32(EmitContext& ctx, IR::Inst* inst) {
    FPTwoOp32(code, ctx, inst, &Xbyak::CodeGenerator::sqrtss);
}

void EmitX64::EmitFPSqrt64(EmitContext& ctx, IR::Inst* inst) {
    FPTwoOp64(code, ctx, inst, &Xbyak::CodeGenerator::sqrtsd);
}

void EmitX64::EmitFPSub32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, &Xbyak::CodeGenerator::subss);
}

void EmitX64::EmitFPSub64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, &Xbyak::CodeGenerator::subsd);
}

static void SetFpscrNzcvFromFlags(BlockOfCode& code, EmitContext& ctx) {
    ctx.reg_alloc.ScratchGpr({HostLoc::RCX}); // shifting requires use of cl
    Xbyak::Reg32 nzcv = ctx.reg_alloc.ScratchGpr().cvt32();

    code.mov(nzcv, 0x28630000);
    code.sete(cl);
    code.rcl(cl, 3);
    code.shl(nzcv, cl);
    code.and_(nzcv, 0xF0000000);
    code.mov(dword[r15 + code.GetJitStateInfo().offsetof_FPSCR_nzcv], nzcv);
}

void EmitX64::EmitFPCompare32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm reg_a = ctx.reg_alloc.UseXmm(args[0]);
    Xbyak::Xmm reg_b = ctx.reg_alloc.UseXmm(args[1]);
    bool exc_on_qnan = args[2].GetImmediateU1();

    if (exc_on_qnan) {
        code.comiss(reg_a, reg_b);
    } else {
        code.ucomiss(reg_a, reg_b);
    }

    SetFpscrNzcvFromFlags(code, ctx);
}

void EmitX64::EmitFPCompare64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm reg_a = ctx.reg_alloc.UseXmm(args[0]);
    Xbyak::Xmm reg_b = ctx.reg_alloc.UseXmm(args[1]);
    bool exc_on_qnan = args[2].GetImmediateU1();

    if (exc_on_qnan) {
        code.comisd(reg_a, reg_b);
    } else {
        code.ucomisd(reg_a, reg_b);
    }

    SetFpscrNzcvFromFlags(code, ctx);
}

void EmitX64::EmitFPSingleToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = ctx.reg_alloc.ScratchGpr();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch.cvt32());
    }
    code.cvtss2sd(result, result);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN64(code, result);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPDoubleToSingle(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = ctx.reg_alloc.ScratchGpr();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
    }
    code.cvtsd2ss(result, result);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero32(code, result, gpr_scratch.cvt32());
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN32(code, result);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPSingleToS32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = ctx.reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = ctx.reg_alloc.ScratchXmm();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // Conversion to double is lossless, and allows for clamping.

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero32(code, from, to);
    }
    code.cvtss2sd(from, from);
    // First time is to set flags
    if (round_towards_zero) {
        code.cvttsd2si(to, from); // 32 bit gpr
    } else {
        code.cvtsd2si(to, from); // 32 bit gpr
    }
    // Clamp to output range
    ZeroIfNaN64(code, from, xmm_scratch);
    code.minsd(from, code.MConst(f64_max_s32));
    code.maxsd(from, code.MConst(f64_min_s32));
    // Second time is for real
    if (round_towards_zero) {
        code.cvttsd2si(to, from); // 32 bit gpr
    } else {
        code.cvtsd2si(to, from); // 32 bit gpr
    }

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPSingleToU32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 to = ctx.reg_alloc.ScratchGpr().cvt64();
    Xbyak::Xmm xmm_scratch = ctx.reg_alloc.ScratchXmm();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // Conversion to double is lossless, and allows for accurate clamping.
    //
    // Since SSE2 doesn't provide an unsigned conversion, we use a 64-bit signed conversion.
    //
    // FIXME: None of the FPSR exception bits are correctly signalled with the below code

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, from, to);
    }
    code.cvtss2sd(from, from);
    // Clamp to output range
    ZeroIfNaN64(code, from, xmm_scratch);
    code.minsd(from, code.MConst(f64_max_u32));
    code.maxsd(from, code.MConst(f64_min_u32));
    if (round_towards_zero) {
        code.cvttsd2si(to, from); // 64 bit gpr
    } else {
        code.cvtsd2si(to, from); // 64 bit gpr
    }

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPDoubleToS32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = ctx.reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = ctx.reg_alloc.ScratchXmm();
    Xbyak::Reg32 gpr_scratch = ctx.reg_alloc.ScratchGpr().cvt32();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, from, gpr_scratch.cvt64());
    }
    // First time is to set flags
    if (round_towards_zero) {
        code.cvttsd2si(gpr_scratch, from); // 32 bit gpr
    } else {
        code.cvtsd2si(gpr_scratch, from); // 32 bit gpr
    }
    // Clamp to output range
    ZeroIfNaN64(code, from, xmm_scratch);
    code.minsd(from, code.MConst(f64_max_s32));
    code.maxsd(from, code.MConst(f64_min_s32));
    // Second time is for real
    if (round_towards_zero) {
        code.cvttsd2si(to, from); // 32 bit gpr
    } else {
        code.cvtsd2si(to, from); // 32 bit gpr
    }

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPDoubleToU32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 to = ctx.reg_alloc.ScratchGpr().cvt64();
    Xbyak::Xmm xmm_scratch = ctx.reg_alloc.ScratchXmm();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // TODO: Use VCVTPD2UDQ when AVX512VL is available.
    // FIXME: None of the FPSR exception bits are correctly signalled with the below code

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, from, to);
    }
    // Clamp to output range
    ZeroIfNaN64(code, from, xmm_scratch);
    code.minsd(from, code.MConst(f64_max_u32));
    code.maxsd(from, code.MConst(f64_min_u32));
    if (round_towards_zero) {
        code.cvttsd2si(to, from); // 64 bit gpr
    } else {
        code.cvtsd2si(to, from); // 64 bit gpr
    }

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPS32ToSingle(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 from = ctx.reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Xmm to = ctx.reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code.cvtsi2ss(to, from);

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPU32ToSingle(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
    Xbyak::Xmm to = ctx.reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    // We are using a 64-bit GPR register to ensure we don't end up treating the input as signed
    code.mov(from.cvt32(), from.cvt32()); // TODO: Verify if this is necessary
    code.cvtsi2ss(to, from);

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPS32ToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 from = ctx.reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Xmm to = ctx.reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code.cvtsi2sd(to, from);

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPU32ToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
    Xbyak::Xmm to = ctx.reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    // We are using a 64-bit GPR register to ensure we don't end up treating the input as signed
    code.mov(from.cvt32(), from.cvt32()); // TODO: Verify if this is necessary
    code.cvtsi2sd(to, from);

    ctx.reg_alloc.DefineValue(inst, to);
}

} // namespace Dynarmic::BackendX64
