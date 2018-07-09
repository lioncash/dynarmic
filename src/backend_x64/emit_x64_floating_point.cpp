/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <type_traits>

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "common/fp_util.h"
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

    auto mask = code.MConst(xword, f64_non_sign_mask);
    mask.setBit(64);
    auto penult_denormal = code.MConst(xword, f64_penultimate_positive_denormal);
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

    auto mask = code.MConst(xword, f64_non_sign_mask);
    mask.setBit(64);
    auto penult_denormal = code.MConst(xword, f64_penultimate_positive_denormal);
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

static void ZeroIfNaN64(BlockOfCode& code, Xbyak::Xmm xmm_value, Xbyak::Xmm xmm_scratch) {
    code.pxor(xmm_scratch, xmm_scratch);
    code.cmpordsd(xmm_scratch, xmm_value); // true mask when ordered (i.e.: when not an NaN)
    code.pand(xmm_value, xmm_scratch);
}

static void PreProcessNaNs32(BlockOfCode& code, Xbyak::Xmm a, Xbyak::Xmm b, Xbyak::Label& end) {
    Xbyak::Label nan;

    code.ucomiss(a, b);
    code.jp(nan, code.T_NEAR);
    code.SwitchToFarCode();
    code.L(nan);

    code.sub(rsp, 8);
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.xor_(code.ABI_PARAM1.cvt32(), code.ABI_PARAM1.cvt32());
    code.xor_(code.ABI_PARAM2.cvt32(), code.ABI_PARAM2.cvt32());
    code.movd(code.ABI_PARAM1.cvt32(), a);
    code.movd(code.ABI_PARAM2.cvt32(), b);
    code.CallFunction(static_cast<u32(*)(u32, u32)>([](u32 a, u32 b) -> u32 {
        return *Common::ProcessNaNs(a, b);
    }));
    code.movd(a, code.ABI_RETURN.cvt32());
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.add(rsp, 8);

    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

static void PreProcessNaNs32(BlockOfCode& code, Xbyak::Xmm a, Xbyak::Xmm b, Xbyak::Xmm c, Xbyak::Label& end) {
    Xbyak::Label nan;

    code.ucomiss(a, b);
    code.jp(nan, code.T_NEAR);
    code.ucomiss(c, c);
    code.jp(nan, code.T_NEAR);
    code.SwitchToFarCode();
    code.L(nan);

    code.sub(rsp, 8);
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.xor_(code.ABI_PARAM1.cvt32(), code.ABI_PARAM1.cvt32());
    code.xor_(code.ABI_PARAM2.cvt32(), code.ABI_PARAM2.cvt32());
    code.xor_(code.ABI_PARAM3.cvt32(), code.ABI_PARAM3.cvt32());
    code.movd(code.ABI_PARAM1.cvt32(), a);
    code.movd(code.ABI_PARAM2.cvt32(), b);
    code.movd(code.ABI_PARAM3.cvt32(), c);
    code.CallFunction(static_cast<u32(*)(u32, u32, u32)>([](u32 a, u32 b, u32 c) -> u32 {
        return *Common::ProcessNaNs(a, b, c);
    }));
    code.movd(a, code.ABI_RETURN.cvt32());
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.add(rsp, 8);

    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

static void PostProcessNaNs32(BlockOfCode& code, Xbyak::Xmm result, Xbyak::Xmm tmp) {
    code.movaps(tmp, result);
    code.cmpunordps(tmp, tmp);
    code.pslld(tmp, 31);
    code.xorps(result, tmp);
}

static void DefaultNaN32(BlockOfCode& code, Xbyak::Xmm xmm_value) {
    Xbyak::Label end;
    code.ucomiss(xmm_value, xmm_value);
    code.jnp(end);
    code.movaps(xmm_value, code.MConst(xword, f32_nan));
    code.L(end);
}

static void PreProcessNaNs64(BlockOfCode& code, Xbyak::Xmm a, Xbyak::Xmm b, Xbyak::Label& end) {
    Xbyak::Label nan;

    code.ucomisd(a, b);
    code.jp(nan, code.T_NEAR);
    code.SwitchToFarCode();
    code.L(nan);

    code.sub(rsp, 8);
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.movq(code.ABI_PARAM1, a);
    code.movq(code.ABI_PARAM2, b);
    code.CallFunction(static_cast<u64(*)(u64, u64)>([](u64 a, u64 b) -> u64 {
        return *Common::ProcessNaNs(a, b);
    }));
    code.movq(a, code.ABI_RETURN);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.add(rsp, 8);

    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

static void PreProcessNaNs64(BlockOfCode& code, Xbyak::Xmm a, Xbyak::Xmm b, Xbyak::Xmm c, Xbyak::Label& end) {
    Xbyak::Label nan;

    code.ucomisd(a, b);
    code.jp(nan, code.T_NEAR);
    code.ucomisd(c, c);
    code.jp(nan, code.T_NEAR);
    code.SwitchToFarCode();
    code.L(nan);

    code.sub(rsp, 8);
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.movq(code.ABI_PARAM1, a);
    code.movq(code.ABI_PARAM2, b);
    code.movq(code.ABI_PARAM3, c);
    code.CallFunction(static_cast<u64(*)(u64, u64, u64)>([](u64 a, u64 b, u64 c) -> u64 {
        return *Common::ProcessNaNs(a, b, c);
    }));
    code.movq(a, code.ABI_RETURN);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(a.getIdx()));
    code.add(rsp, 8);

    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

static void PostProcessNaNs64(BlockOfCode& code, Xbyak::Xmm result, Xbyak::Xmm tmp) {
    code.movaps(tmp, result);
    code.cmpunordpd(tmp, tmp);
    code.psllq(tmp, 63);
    code.xorps(result, tmp);
}

static void DefaultNaN64(BlockOfCode& code, Xbyak::Xmm xmm_value) {
    Xbyak::Label end;
    code.ucomisd(xmm_value, xmm_value);
    code.jnp(end);
    code.movaps(xmm_value, code.MConst(xword, f64_nan));
    code.L(end);
}

static Xbyak::Label ProcessNaN32(BlockOfCode& code, Xbyak::Xmm a) {
    Xbyak::Label nan, end;

    code.ucomiss(a, a);
    code.jp(nan, code.T_NEAR);
    code.SwitchToFarCode();
    code.L(nan);

    code.orps(a, code.MConst(xword, 0x00400000));

    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
    return end;
}

static Xbyak::Label ProcessNaN64(BlockOfCode& code, Xbyak::Xmm a) {
    Xbyak::Label nan, end;

    code.ucomisd(a, a);
    code.jp(nan, code.T_NEAR);
    code.SwitchToFarCode();
    code.L(nan);

    code.orps(a, code.MConst(xword, 0x0008'0000'0000'0000));

    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
    return end;
}

template <typename PreprocessFunction, typename Function>
static void FPThreeOp32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, [[maybe_unused]] PreprocessFunction preprocess, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Label end;

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Reg32 gpr_scratch = ctx.reg_alloc.ScratchGpr().cvt32();

    if constexpr(!std::is_same_v<PreprocessFunction, std::nullptr_t>) {
        preprocess(result, operand, gpr_scratch, end);
    }
    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
        DenormalsAreZero32(code, operand, gpr_scratch);
    }
    if (ctx.AccurateNaN() && !ctx.FPSCR_DN()) {
        PreProcessNaNs32(code, result, operand, end);
    }
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, operand);
    } else {
        fn(result, operand);
    }
    if (ctx.FPSCR_FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN32(code, result);
    } else if (ctx.AccurateNaN()) {
        PostProcessNaNs32(code, result, operand);
    }
    code.L(end);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename PreprocessFunction, typename Function>
static void FPThreeOp64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, [[maybe_unused]] PreprocessFunction preprocess, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Label end;

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Reg64 gpr_scratch = ctx.reg_alloc.ScratchGpr();

    if constexpr(!std::is_same_v<PreprocessFunction, std::nullptr_t>) {
        preprocess(result, operand, gpr_scratch, end);
    }
    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
        DenormalsAreZero64(code, operand, gpr_scratch);
    }
    if (ctx.AccurateNaN() && !ctx.FPSCR_DN()) {
        PreProcessNaNs64(code, result, operand, end);
    }
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, operand);
    } else {
        fn(result, operand);
    }
    if (ctx.FPSCR_FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN64(code, result);
    } else if (ctx.AccurateNaN()) {
        PostProcessNaNs64(code, result, operand);
    }
    code.L(end);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename Function>
static void FPThreeOp32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    FPThreeOp32(code, ctx, inst, nullptr, fn);
}

template <typename Function>
static void FPThreeOp64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    FPThreeOp64(code, ctx, inst, nullptr, fn);
}

template <typename Function>
static void FPTwoOp32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Label end;

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 gpr_scratch = ctx.reg_alloc.ScratchGpr().cvt32();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
    }
    if (ctx.AccurateNaN() && !ctx.FPSCR_DN()) {
        end = ProcessNaN32(code, result);
    }
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, result);
    } else {
        fn(result);
    }
    if (ctx.FPSCR_FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN32(code, result);
    } else if (ctx.AccurateNaN()) {
        PostProcessNaNs32(code, result, ctx.reg_alloc.ScratchXmm());
    }
    code.L(end);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename Function>
static void FPTwoOp64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Label end;

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = ctx.reg_alloc.ScratchGpr();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
    }
    if (ctx.AccurateNaN() && !ctx.FPSCR_DN()) {
        end = ProcessNaN64(code, result);
    }
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, result);
    } else {
        fn(result);
    }
    if (ctx.FPSCR_FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN64(code, result);
    } else if (ctx.AccurateNaN()) {
        PostProcessNaNs64(code, result, ctx.reg_alloc.ScratchXmm());
    }
    code.L(end);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename Function>
static void FPFourOp32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Label end;

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand2 = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Xmm operand3 = ctx.reg_alloc.UseScratchXmm(args[2]);
    Xbyak::Reg32 gpr_scratch = ctx.reg_alloc.ScratchGpr().cvt32();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
        DenormalsAreZero32(code, operand2, gpr_scratch);
        DenormalsAreZero32(code, operand3, gpr_scratch);
    }
    if (ctx.AccurateNaN() && !ctx.FPSCR_DN()) {
        PreProcessNaNs32(code, result, operand2, operand3, end);
    }
    fn(result, operand2, operand3);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN32(code, result);
    } else if (ctx.AccurateNaN()) {
        PostProcessNaNs32(code, result, operand2);
    }
    code.L(end);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename Function>
static void FPFourOp64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Label end;

    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand2 = ctx.reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Xmm operand3 = ctx.reg_alloc.UseScratchXmm(args[2]);
    Xbyak::Reg64 gpr_scratch = ctx.reg_alloc.ScratchGpr();

    if (ctx.FPSCR_FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
        DenormalsAreZero64(code, operand2, gpr_scratch);
        DenormalsAreZero64(code, operand3, gpr_scratch);
    }
    if (ctx.AccurateNaN() && !ctx.FPSCR_DN()) {
        PreProcessNaNs64(code, result, operand2, operand3, end);
    }
    fn(result, operand2, operand3);
    if (ctx.FPSCR_FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (ctx.FPSCR_DN()) {
        DefaultNaN64(code, result);
    } else if (ctx.AccurateNaN()) {
        PostProcessNaNs64(code, result, operand2);
    }
    code.L(end);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPAbs32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pand(result, code.MConst(xword, f32_non_sign_mask));

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPAbs64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pand(result, code.MConst(xword, f64_non_sign_mask));

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPNeg32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pxor(result, code.MConst(xword, f32_negative_zero));

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPNeg64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.pxor(result, code.MConst(xword, f64_negative_zero));

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

void EmitX64::EmitFPMax32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand){
        Xbyak::Label normal, end;
        code.ucomiss(result, operand);
        code.jnz(normal);
        if (!ctx.AccurateNaN()) {
            Xbyak::Label notnan;
            code.jnp(notnan);
            code.addss(result, operand);
            code.jmp(end);
            code.L(notnan);
        }
        code.andps(result, operand);
        code.jmp(end);
        code.L(normal);
        code.maxss(result, operand);
        code.L(end);
    });
}

void EmitX64::EmitFPMax64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand){
        Xbyak::Label normal, end;
        code.ucomisd(result, operand);
        code.jnz(normal);
        if (!ctx.AccurateNaN()) {
            Xbyak::Label notnan;
            code.jnp(notnan);
            code.addsd(result, operand);
            code.jmp(end);
            code.L(notnan);
        }
        code.andps(result, operand);
        code.jmp(end);
        code.L(normal);
        code.maxsd(result, operand);
        code.L(end);
    });
}

void EmitX64::EmitFPMaxNumeric32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand, Xbyak::Reg32 scratch, Xbyak::Label& end){
        Xbyak::Label normal, normal_or_equal, result_is_result;

        code.ucomiss(result, operand);
        code.jnp(normal_or_equal);
        // If operand == QNaN, result = result.
        code.movd(scratch, operand);
        code.shl(scratch, 1);
        code.cmp(scratch, 0xff800000u);
        code.jae(result_is_result);
        // If operand == SNaN, let usual NaN code handle it.
        code.cmp(scratch, 0xff000000u);
        code.ja(normal);
        // If result == SNaN, && operand != NaN, result = result.
        code.movd(scratch, result);
        code.shl(scratch, 1);
        code.cmp(scratch, 0xff800000u);
        code.jnae(result_is_result);
        // If result == QNaN && operand != NaN, result = operand.
        code.movaps(result, operand);
        code.jmp(end, code.T_NEAR);

        code.L(result_is_result);
        code.movaps(operand, result);
        code.jmp(normal);

        code.L(normal_or_equal);
        code.jnz(normal);
        code.andps(operand, result);
        code.L(normal);
    }, &Xbyak::CodeGenerator::maxss);
}

void EmitX64::EmitFPMaxNumeric64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand, Xbyak::Reg64 scratch, Xbyak::Label& end){
        Xbyak::Label normal, normal_or_equal, result_is_result;

        code.ucomisd(result, operand);
        code.jnp(normal_or_equal);
        // If operand == QNaN, result = result.
        code.movq(scratch, operand);
        code.shl(scratch, 1);
        code.cmp(scratch, code.MConst(qword, 0xfff0'0000'0000'0000u));
        code.jae(result_is_result);
        // If operand == SNaN, let usual NaN code handle it.
        code.cmp(scratch, code.MConst(qword, 0xffe0'0000'0000'0000u));
        code.ja(normal);
        // If result == SNaN, && operand != NaN, result = result.
        code.movq(scratch, result);
        code.shl(scratch, 1);
        code.cmp(scratch, code.MConst(qword, 0xfff0'0000'0000'0000u));
        code.jnae(result_is_result);
        // If result == QNaN && operand != NaN, result = operand.
        code.movaps(result, operand);
        code.jmp(end, code.T_NEAR);

        code.L(result_is_result);
        code.movaps(operand, result);
        code.jmp(normal);

        code.L(normal_or_equal);
        code.jnz(normal);
        code.andps(operand, result);
        code.L(normal);
    }, &Xbyak::CodeGenerator::maxsd);
}

void EmitX64::EmitFPMin32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand){
        Xbyak::Label normal, end;
        code.ucomiss(result, operand);
        code.jnz(normal);
        code.orps(result, operand);
        code.jmp(end);
        code.L(normal);
        code.minss(result, operand);
        code.L(end);
    });
}

void EmitX64::EmitFPMin64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand){
        Xbyak::Label normal, end;
        code.ucomisd(result, operand);
        code.jnz(normal);
        code.orps(result, operand);
        code.jmp(end);
        code.L(normal);
        code.minsd(result, operand);
        code.L(end);
    });
}


void EmitX64::EmitFPMinNumeric32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand, Xbyak::Reg32 scratch, Xbyak::Label& end){
        Xbyak::Label normal, normal_or_equal, result_is_result;

        code.ucomiss(result, operand);
        code.jnp(normal_or_equal);
        // If operand == QNaN, result = result.
        code.movd(scratch, operand);
        code.shl(scratch, 1);
        code.cmp(scratch, 0xff800000u);
        code.jae(result_is_result);
        // If operand == SNaN, let usual NaN code handle it.
        code.cmp(scratch, 0xff000000u);
        code.ja(normal);
        // If result == SNaN, && operand != NaN, result = result.
        code.movd(scratch, result);
        code.shl(scratch, 1);
        code.cmp(scratch, 0xff800000u);
        code.jnae(result_is_result);
        // If result == QNaN && operand != NaN, result = operand.
        code.movaps(result, operand);
        code.jmp(end, code.T_NEAR);

        code.L(result_is_result);
        code.movaps(operand, result);
        code.jmp(normal);

        code.L(normal_or_equal);
        code.jnz(normal);
        code.orps(operand, result);
        code.L(normal);
    }, &Xbyak::CodeGenerator::minss);
}

void EmitX64::EmitFPMinNumeric64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand, Xbyak::Reg64 scratch, Xbyak::Label& end){
        Xbyak::Label normal, normal_or_equal, result_is_result;

        code.ucomisd(result, operand);
        code.jnp(normal_or_equal);
        // If operand == QNaN, result = result.
        code.movq(scratch, operand);
        code.shl(scratch, 1);
        code.cmp(scratch, code.MConst(qword, 0xfff0'0000'0000'0000u));
        code.jae(result_is_result);
        // If operand == SNaN, let usual NaN code handle it.
        code.cmp(scratch, code.MConst(qword, 0xffe0'0000'0000'0000u));
        code.ja(normal);
        // If result == SNaN, && operand != NaN, result = result.
        code.movq(scratch, result);
        code.shl(scratch, 1);
        code.cmp(scratch, code.MConst(qword, 0xfff0'0000'0000'0000u));
        code.jnae(result_is_result);
        // If result == QNaN && operand != NaN, result = operand.
        code.movaps(result, operand);
        code.jmp(end, code.T_NEAR);

        code.L(result_is_result);
        code.movaps(operand, result);
        code.jmp(normal);

        code.L(normal_or_equal);
        code.jnz(normal);
        code.orps(operand, result);
        code.L(normal);
    }, &Xbyak::CodeGenerator::minsd);
}

void EmitX64::EmitFPMul32(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp32(code, ctx, inst, &Xbyak::CodeGenerator::mulss);
}

void EmitX64::EmitFPMul64(EmitContext& ctx, IR::Inst* inst) {
    FPThreeOp64(code, ctx, inst, &Xbyak::CodeGenerator::mulsd);
}

void EmitX64::EmitFPMulAdd32(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tFMA)) {
        FPFourOp32(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand2, Xbyak::Xmm operand3) {
            code.vfmadd231ss(result, operand2, operand3);
        });
        return;
    }

    // TODO: Improve accuracy.
    FPFourOp32(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand2, Xbyak::Xmm operand3) {
        code.mulss(operand2, operand3);
        code.addss(result, operand2);
    });
}

void EmitX64::EmitFPMulAdd64(EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tFMA)) {
        FPFourOp64(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand2, Xbyak::Xmm operand3) {
            code.vfmadd231sd(result, operand2, operand3);
        });
        return;
    }

    // TODO: Improve accuracy.
    FPFourOp64(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm operand2, Xbyak::Xmm operand3) {
        code.mulsd(operand2, operand3);
        code.addsd(result, operand2);
    });
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

static Xbyak::Reg64 SetFpscrNzcvFromFlags(BlockOfCode& code, EmitContext& ctx) {
    ctx.reg_alloc.ScratchGpr({HostLoc::RCX}); // shifting requires use of cl
    Xbyak::Reg64 nzcv = ctx.reg_alloc.ScratchGpr();

    //               x64 flags    ARM flags
    //               ZF  PF  CF     NZCV
    // Unordered      1   1   1     0011
    // Greater than   0   0   0     0010
    // Less than      0   0   1     1000
    // Equal          1   0   0     0110
    //
    // Thus we can take use ZF:CF as an index into an array like so:
    //  x64      ARM      ARM as x64
    // ZF:CF     NZCV     NZ-----C-------V
    //   0       0010     0000000100000000 = 0x0100
    //   1       1000     1000000000000000 = 0x8000
    //   2       0110     0100000100000000 = 0x4100
    //   3       0011     0000000100000001 = 0x0101

    code.mov(nzcv, 0x0101'4100'8000'0100);
    code.sete(cl);
    code.rcl(cl, 5); // cl = ZF:CF:0000
    code.shr(nzcv, cl);

    return nzcv;
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

    Xbyak::Reg64 nzcv = SetFpscrNzcvFromFlags(code, ctx);
    ctx.reg_alloc.DefineValue(inst, nzcv);
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

    Xbyak::Reg64 nzcv = SetFpscrNzcvFromFlags(code, ctx);
    ctx.reg_alloc.DefineValue(inst, nzcv);
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
    code.minsd(from, code.MConst(xword, f64_max_s32));
    code.maxsd(from, code.MConst(xword, f64_min_s32));
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
    code.minsd(from, code.MConst(xword, f64_max_u32));
    code.maxsd(from, code.MConst(xword, f64_min_u32));
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
    code.minsd(from, code.MConst(xword, f64_max_s32));
    code.maxsd(from, code.MConst(xword, f64_min_s32));
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
    code.minsd(from, code.MConst(xword, f64_max_u32));
    code.maxsd(from, code.MConst(xword, f64_min_u32));
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

    const Xbyak::Xmm to = ctx.reg_alloc.ScratchXmm();
    const bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512F)) {
        const Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
        code.vcvtusi2ss(to, to, from.cvt32());
    } else {
        // We are using a 64-bit GPR register to ensure we don't end up treating the input as signed
        const Xbyak::Reg64 from = ctx.reg_alloc.UseScratchGpr(args[0]);
        code.mov(from.cvt32(), from.cvt32()); // TODO: Verify if this is necessary
        code.cvtsi2ss(to, from);
    }

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

void EmitX64::EmitFPS64ToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    const bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code.cvtsi2sd(result, from);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPS64ToSingle(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    const bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code.cvtsi2ss(result, from);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPU32ToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm to = ctx.reg_alloc.ScratchXmm();
    const bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512F)) {
        const Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
        code.vcvtusi2sd(to, to, from.cvt32());
    } else {
        // We are using a 64-bit GPR register to ensure we don't end up treating the input as signed
        const Xbyak::Reg64 from = ctx.reg_alloc.UseScratchGpr(args[0]);
        code.mov(from.cvt32(), from.cvt32()); // TODO: Verify if this is necessary
        code.cvtsi2sd(to, from);
    }

    ctx.reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPU64ToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    const bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512F)) {
        code.vcvtusi2sd(result, result, from);
    } else {
        const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        code.movq(tmp, from);
        code.punpckldq(tmp, code.MConst(xword, 0x4530000043300000, 0));
        code.subpd(tmp, code.MConst(xword, 0x4330000000000000, 0x4530000000000000));
        code.pshufd(result, tmp, 0b01001110);
        code.addpd(result, tmp);
        if (ctx.FPSCR_RMode() == FP::RoundingMode::TowardsMinusInfinity) {
            code.pand(result, code.MConst(xword, f64_non_sign_mask));
        }
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPU64ToSingle(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    const bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512F)) {
        const Xbyak::Reg64 from = ctx.reg_alloc.UseGpr(args[0]);
        code.vcvtusi2ss(result, result, from);
    } else {
        const Xbyak::Reg64 from = ctx.reg_alloc.UseScratchGpr(args[0]);
        code.pxor(result, result);

        Xbyak::Label negative;
        Xbyak::Label end;

        code.test(from, from);
        code.js(negative);

        code.cvtsi2ss(result, from);
        code.jmp(end);

        code.L(negative);
        const Xbyak::Reg64 tmp = ctx.reg_alloc.ScratchGpr();
        code.mov(tmp, from);
        code.shr(tmp, 1);
        code.and_(from.cvt32(), 1);
        code.or_(from, tmp);
        code.cvtsi2ss(result, from);
        code.addss(result, result);

        code.L(end);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}
} // namespace Dynarmic::BackendX64
