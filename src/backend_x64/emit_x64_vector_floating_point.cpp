/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <type_traits>

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/bit_util.h"
#include "common/fp_util.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;

template <typename T>
struct NaNWrapper;

template <>
struct NaNWrapper<u32> {
    static constexpr u32 value = 0x7fc00000;
};

template <>
struct NaNWrapper<u64> {
    static constexpr u64 value = 0x7ff8'0000'0000'0000;
};

template <typename T>
static void HandleNaNs(BlockOfCode& code, EmitContext& ctx, const Xbyak::Xmm& xmm_a,
                       const Xbyak::Xmm& xmm_b, const Xbyak::Xmm& result, const Xbyak::Xmm& nan_mask) {
    static_assert(std::is_same_v<T, u32> || std::is_same_v<T, u64>, "T must be either u32 or u64");

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.ptest(nan_mask, nan_mask);
    } else {
        const Xbyak::Reg32 bitmask = ctx.reg_alloc.ScratchGpr().cvt32();
        code.movmskps(bitmask, nan_mask);
        code.cmp(bitmask, 0);
    }

    Xbyak::Label end;
    Xbyak::Label nan;

    code.jz(end);
    code.jmp(nan, code.T_NEAR);
    code.L(end);

    code.SwitchToFarCode();
    code.L(nan);
    code.sub(rsp, 8);
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
    const size_t stack_space = 3 * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE + 2 * 16]);
    code.movaps(xword[code.ABI_PARAM1], result);
    code.movaps(xword[code.ABI_PARAM2], xmm_a);
    code.movaps(xword[code.ABI_PARAM3], xmm_b);

    using Elements = std::integral_constant<size_t, 128 / Common::BitSize<T>()>;
    using RegArray = std::array<T, Elements::value>;
    code.CallFunction(static_cast<void(*)(RegArray&, const RegArray&, const RegArray&)>(
        [](RegArray& result, const RegArray& a, const RegArray& b) {
            for (size_t i = 0; i < result.size(); ++i) {
                if (auto r = Common::ProcessNaNs(a[i], b[i])) {
                    result[i] = *r;
                } else if (Common::IsNaN(result[i])) {
                    result[i] = NaNWrapper<T>::value;
                }
            }
        }
    ));

    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.add(rsp, stack_space + ABI_SHADOW_SPACE);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
    code.add(rsp, 8);
    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

template <typename Function>
static void EmitVectorOperation32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    if (!ctx.AccurateNaN() || ctx.FPSCR_DN()) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

        (code.*fn)(xmm_a, xmm_b);

        if (ctx.FPSCR_DN()) {
            Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();
            Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
            code.pcmpeqw(tmp, tmp);
            code.movaps(nan_mask, xmm_a);
            code.cmpordps(nan_mask, nan_mask);
            code.andps(xmm_a, nan_mask);
            code.xorps(nan_mask, tmp);
            code.andps(nan_mask, code.MConst(xword, 0x7fc0'0000'7fc0'0000, 0x7fc0'0000'7fc0'0000));
            code.orps(xmm_a, nan_mask);
        }

        ctx.reg_alloc.DefineValue(inst, xmm_a);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();

    code.movaps(nan_mask, xmm_b);
    code.movaps(result, xmm_a);
    code.cmpunordps(nan_mask, xmm_a);
    (code.*fn)(result, xmm_b);
    code.cmpunordps(nan_mask, result);

    HandleNaNs<u32>(code, ctx, xmm_a, xmm_b, result, nan_mask);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename Function>
static void EmitVectorOperation64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    if (!ctx.AccurateNaN() || ctx.FPSCR_DN()) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

        (code.*fn)(xmm_a, xmm_b);

        if (ctx.FPSCR_DN()) {
            Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();
            Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
            code.pcmpeqw(tmp, tmp);
            code.movaps(nan_mask, xmm_a);
            code.cmpordpd(nan_mask, nan_mask);
            code.andps(xmm_a, nan_mask);
            code.xorps(nan_mask, tmp);
            code.andps(nan_mask, code.MConst(xword, 0x7ff8'0000'0000'0000, 0x7ff8'0000'0000'0000));
            code.orps(xmm_a, nan_mask);
        }

        ctx.reg_alloc.DefineValue(inst, xmm_a);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Label end, nan;
    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();

    code.movaps(nan_mask, xmm_b);
    code.movaps(result, xmm_a);
    code.cmpunordpd(nan_mask, xmm_a);
    (code.*fn)(result, xmm_b);
    code.cmpunordpd(nan_mask, result);

    HandleNaNs<u64>(code, ctx, xmm_a, xmm_b, result, nan_mask);

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPVectorAbsoluteDifference32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    code.subps(a, b);
    code.andps(a, code.MConst(xword, 0x7FFFFFFF7FFFFFFF, 0x7FFFFFFF7FFFFFFF));

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorAbsoluteDifference64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    code.subpd(a, b);
    code.andpd(a, code.MConst(xword, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF));

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorAbs16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Address mask = code.MConst(xword, 0x7FFF7FFF7FFF7FFF, 0x7FFF7FFF7FFF7FFF);

    code.pand(a, mask);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorAbs32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Address mask = code.MConst(xword, 0x7FFFFFFF7FFFFFFF, 0x7FFFFFFF7FFFFFFF);

    code.andps(a, mask);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorAbs64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Address mask = code.MConst(xword, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF);

    code.andpd(a, mask);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32(code, ctx, inst, &Xbyak::CodeGenerator::addps);
}

void EmitX64::EmitFPVectorAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64(code, ctx, inst, &Xbyak::CodeGenerator::addpd);
}

void EmitX64::EmitFPVectorDiv32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32(code, ctx, inst, &Xbyak::CodeGenerator::divps);
}

void EmitX64::EmitFPVectorDiv64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64(code, ctx, inst, &Xbyak::CodeGenerator::divpd);
}

void EmitX64::EmitFPVectorEqual32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    code.cmpeqps(a, b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorEqual64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseXmm(args[1]);

    code.cmpeqpd(a, b);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorGreater32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.cmpltps(b, a);

    ctx.reg_alloc.DefineValue(inst, b);
}

void EmitX64::EmitFPVectorGreater64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.cmpltpd(b, a);

    ctx.reg_alloc.DefineValue(inst, b);
}

void EmitX64::EmitFPVectorGreaterEqual32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.cmpleps(b, a);

    ctx.reg_alloc.DefineValue(inst, b);
}

void EmitX64::EmitFPVectorGreaterEqual64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm b = ctx.reg_alloc.UseScratchXmm(args[1]);

    code.cmplepd(b, a);

    ctx.reg_alloc.DefineValue(inst, b);
}

void EmitX64::EmitFPVectorMul32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32(code, ctx, inst, &Xbyak::CodeGenerator::mulps);
}

void EmitX64::EmitFPVectorMul64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64(code, ctx, inst, &Xbyak::CodeGenerator::mulpd);
}

void EmitX64::EmitFPVectorS32ToSingle(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);

    code.cvtdq2ps(xmm, xmm);

    ctx.reg_alloc.DefineValue(inst, xmm);
}

void EmitX64::EmitFPVectorS64ToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL) && code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512DQ)) {
        code.vcvtqq2pd(xmm, xmm);
    } else if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        const Xbyak::Xmm xmm_tmp = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Reg64 tmp = ctx.reg_alloc.ScratchGpr();

        // First quadword
        code.movq(tmp, xmm);
        code.cvtsi2sd(xmm, tmp);

        // Second quadword
        code.pextrq(tmp, xmm, 1);
        code.cvtsi2sd(xmm_tmp, tmp);

        // Combine
        code.unpcklpd(xmm, xmm_tmp);
    } else {
        const Xbyak::Xmm high_xmm = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm xmm_tmp = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Reg64 tmp = ctx.reg_alloc.ScratchGpr();

        // First quadword
        code.movhlps(high_xmm, xmm);
        code.movq(tmp, xmm);
        code.cvtsi2sd(xmm, tmp);

        // Second quadword
        code.movq(tmp, high_xmm);
        code.cvtsi2sd(xmm_tmp, tmp);

        // Combine
        code.unpcklpd(xmm, xmm_tmp);
    }

    ctx.reg_alloc.DefineValue(inst, xmm);
}

void EmitX64::EmitFPVectorSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32(code, ctx, inst, &Xbyak::CodeGenerator::subps);
}

void EmitX64::EmitFPVectorSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64(code, ctx, inst, &Xbyak::CodeGenerator::subpd);
}

} // namespace Dynarmic::BackendX64
