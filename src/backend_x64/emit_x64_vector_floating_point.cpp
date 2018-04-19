/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/fp_util.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;

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

    Xbyak::Label end, nan;
    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();

    code.movaps(nan_mask, xmm_b);
    code.movaps(result, xmm_a);
    code.cmpunordps(nan_mask, xmm_a);
    (code.*fn)(result, xmm_b);
    code.cmpunordps(nan_mask, result);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.ptest(nan_mask, nan_mask);
    } else {
        Xbyak::Reg32 bitmask = ctx.reg_alloc.ScratchGpr().cvt32();
        code.movmskps(bitmask, nan_mask);
        code.cmp(bitmask, 0);
    }
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
    code.CallFunction(static_cast<void(*)(std::array<u32, 4>&, const std::array<u32, 4>&, const std::array<u32, 4>&)>(
        [](std::array<u32, 4>& result, const std::array<u32, 4>& a, const std::array<u32, 4>& b) {
            for (size_t i = 0; i < 4; ++i) {
                if (auto r = Common::ProcessNaNs(a[i], b[i])) {
                    result[i] = *r;
                } else if (Common::IsNaN(result[i])) {
                    result[i] = 0x7fc00000;
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
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.ptest(nan_mask, nan_mask);
    } else {
        Xbyak::Reg32 bitmask = ctx.reg_alloc.ScratchGpr().cvt32();
        code.movmskps(bitmask, nan_mask);
        code.cmp(bitmask, 0);
    }
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
    code.CallFunction(static_cast<void(*)(std::array<u64, 2>&, const std::array<u64, 2>&, const std::array<u64, 2>&)>(
        [](std::array<u64, 2>& result, const std::array<u64, 2>& a, const std::array<u64, 2>& b) {
            for (size_t i = 0; i < 2; ++i) {
                if (auto r = Common::ProcessNaNs(a[i], b[i])) {
                    result[i] = *r;
                } else if (Common::IsNaN(result[i])) {
                    result[i] = 0x7ff8'0000'0000'0000;
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

    ctx.reg_alloc.DefineValue(inst, result);
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

void EmitX64::EmitFPVectorSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32(code, ctx, inst, &Xbyak::CodeGenerator::subps);
}

void EmitX64::EmitFPVectorSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64(code, ctx, inst, &Xbyak::CodeGenerator::subpd);
}

} // namespace Dynarmic::BackendX64
