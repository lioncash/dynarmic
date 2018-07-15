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
#include "common/fp/util.h"
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

template <typename T, auto IndexFunction>
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
                auto [first, second] = IndexFunction(i, a, b);
                if (auto r = FP::ProcessNaNs(first, second)) {
                    result[i] = *r;
                } else if (FP::IsNaN(result[i])) {
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

static std::tuple<u32, u32> DefaultIndexFunction32(size_t i, const std::array<u32, 4>& a, const std::array<u32, 4>& b) {
    return std::make_tuple(a[i], b[i]);
}

static std::tuple<u64, u64> DefaultIndexFunction64(size_t i, const std::array<u64, 2>& a, const std::array<u64, 2>& b) {
    return std::make_tuple(a[i], b[i]);
}

static std::tuple<u32, u32> PairedIndexFunction32(size_t i, const std::array<u32, 4>& a, const std::array<u32, 4>& b) {
    if (i < 2) {
        return std::make_tuple(a[2 * i], a[2 * i + 1]);
    }
    return std::make_tuple(b[2 * (i - 2)], b[2 * (i - 2) + 1]);
}

static std::tuple<u64, u64> PairedIndexFunction64(size_t i, const std::array<u64, 2>& a, const std::array<u64, 2>& b) {
    return i == 0 ? std::make_tuple(a[0], a[1]) : std::make_tuple(b[0], b[1]);
}

static std::tuple<u32, u32> PairedLowerIndexFunction32(size_t i, const std::array<u32, 4>& a, const std::array<u32, 4>& b) {
    switch (i) {
    case 0:
        return std::make_tuple(a[0], a[1]);
    case 1:
        return std::make_tuple(b[0], b[1]);
    default:
        return std::make_tuple(u32(0), u32(0));
    }
}

static std::tuple<u64, u64> PairedLowerIndexFunction64(size_t i, const std::array<u64, 2>& a, const std::array<u64, 2>& b) {
    return i == 0 ? std::make_tuple(a[0], b[0]) : std::make_tuple(u64(0), u64(0));
}

template <auto IndexFunction, typename Function>
static void EmitVectorOperation32(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    if (!ctx.AccurateNaN() || ctx.FPSCR_DN()) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

        if constexpr (std::is_member_function_pointer_v<Function>) {
            (code.*fn)(xmm_a, xmm_b);
        } else {
            fn(xmm_a, xmm_b);
        }

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
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, xmm_b);
    } else {
        fn(result, xmm_b);
    }
    code.cmpunordps(nan_mask, result);

    HandleNaNs<u32, IndexFunction>(code, ctx, xmm_a, xmm_b, result, nan_mask);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <auto IndexFunction, typename Function>
static void EmitVectorOperation64(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    if (!ctx.AccurateNaN() || ctx.FPSCR_DN()) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

        if constexpr (std::is_member_function_pointer_v<Function>) {
            (code.*fn)(xmm_a, xmm_b);
        } else {
            fn(xmm_a, xmm_b);
        }

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
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, xmm_b);
    } else {
        fn(result, xmm_b);
    }
    code.cmpunordpd(nan_mask, result);

    HandleNaNs<u64, IndexFunction>(code, ctx, xmm_a, xmm_b, result, nan_mask);

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
    EmitVectorOperation32<DefaultIndexFunction32>(code, ctx, inst, &Xbyak::CodeGenerator::addps);
}

void EmitX64::EmitFPVectorAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64<DefaultIndexFunction64>(code, ctx, inst, &Xbyak::CodeGenerator::addpd);
}

void EmitX64::EmitFPVectorDiv32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32<DefaultIndexFunction32>(code, ctx, inst, &Xbyak::CodeGenerator::divps);
}

void EmitX64::EmitFPVectorDiv64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64<DefaultIndexFunction64>(code, ctx, inst, &Xbyak::CodeGenerator::divpd);
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
    EmitVectorOperation32<DefaultIndexFunction32>(code, ctx, inst, &Xbyak::CodeGenerator::mulps);
}

void EmitX64::EmitFPVectorMul64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64<DefaultIndexFunction64>(code, ctx, inst, &Xbyak::CodeGenerator::mulpd);
}

void EmitX64::EmitFPVectorPairedAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32<PairedIndexFunction32>(code, ctx, inst, &Xbyak::CodeGenerator::haddps);
}

void EmitX64::EmitFPVectorPairedAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64<PairedIndexFunction64>(code, ctx, inst, &Xbyak::CodeGenerator::haddpd);
}

void EmitX64::EmitFPVectorPairedAddLower32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation32<PairedLowerIndexFunction32>(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm xmm_b) {
        const Xbyak::Xmm zero = ctx.reg_alloc.ScratchXmm();
        code.xorps(zero, zero);
        code.punpcklqdq(result, xmm_b);
        code.haddps(result, zero);
    });
}

void EmitX64::EmitFPVectorPairedAddLower64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64<PairedLowerIndexFunction64>(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm xmm_b) {
        const Xbyak::Xmm zero = ctx.reg_alloc.ScratchXmm();
        code.xorps(zero, zero);
        code.punpcklqdq(result, xmm_b); 
        code.haddpd(result, zero);
    });
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
    EmitVectorOperation32<DefaultIndexFunction32>(code, ctx, inst, &Xbyak::CodeGenerator::subps);
}

void EmitX64::EmitFPVectorSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation64<DefaultIndexFunction64>(code, ctx, inst, &Xbyak::CodeGenerator::subpd);
}

void EmitX64::EmitFPVectorU32ToSingle(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512DQ) && code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        code.vcvtudq2ps(xmm, xmm);
    } else {
        const Xbyak::Address mem_4B000000 = code.MConst(xword, 0x4B0000004B000000, 0x4B0000004B000000);
        const Xbyak::Address mem_53000000 = code.MConst(xword, 0x5300000053000000, 0x5300000053000000);
        const Xbyak::Address mem_D3000080 = code.MConst(xword, 0xD3000080D3000080, 0xD3000080D3000080);

        const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
            code.vpblendw(tmp, xmm, mem_4B000000, 0b10101010);
            code.vpsrld(xmm, xmm, 16);
            code.vpblendw(xmm, xmm, mem_53000000, 0b10101010);
            code.vaddps(xmm, xmm, mem_D3000080);
            code.vaddps(xmm, tmp, xmm);
        } else {
            const Xbyak::Address mem_0xFFFF = code.MConst(xword, 0x0000FFFF0000FFFF, 0x0000FFFF0000FFFF);

            code.movdqa(tmp, mem_0xFFFF);

            code.pand(tmp, xmm);
            code.por(tmp, mem_4B000000);
            code.psrld(xmm, 16);
            code.por(xmm, mem_53000000);
            code.addps(xmm, mem_D3000080);
            code.addps(xmm, tmp);
        }
    }

    if (ctx.FPSCR_RMode() == FP::RoundingMode::TowardsMinusInfinity) {
        code.pand(xmm, code.MConst(xword, 0x7FFFFFFF7FFFFFFF, 0x7FFFFFFF7FFFFFFF));
    }

    ctx.reg_alloc.DefineValue(inst, xmm);
}

void EmitX64::EmitFPVectorU64ToDouble(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512DQ) && code.DoesCpuSupport(Xbyak::util::Cpu::tAVX512VL)) {
        code.vcvtuqq2pd(xmm, xmm);
    } else {
        const Xbyak::Address unpack = code.MConst(xword, 0x4530000043300000, 0);
        const Xbyak::Address subtrahend = code.MConst(xword, 0x4330000000000000, 0x4530000000000000);

        const Xbyak::Xmm unpack_reg = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm subtrahend_reg = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm tmp1 = ctx.reg_alloc.ScratchXmm();

        if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
            code.vmovapd(unpack_reg, unpack);
            code.vmovapd(subtrahend_reg, subtrahend);

            code.vunpcklps(tmp1, xmm, unpack_reg);
            code.vsubpd(tmp1, tmp1, subtrahend_reg);

            code.vpermilps(xmm, xmm, 0b01001110);

            code.vunpcklps(xmm, xmm, unpack_reg);
            code.vsubpd(xmm, xmm, subtrahend_reg);

            code.vhaddpd(xmm, tmp1, xmm);
        } else {
            const Xbyak::Xmm tmp2 = ctx.reg_alloc.ScratchXmm();

            code.movapd(unpack_reg, unpack);
            code.movapd(subtrahend_reg, subtrahend);

            code.pshufd(tmp1, xmm, 0b01001110);

            code.punpckldq(xmm, unpack_reg);
            code.subpd(xmm, subtrahend_reg);
            code.pshufd(tmp2, xmm, 0b01001110);
            code.addpd(xmm, tmp2);

            code.punpckldq(tmp1, unpack_reg);
            code.subpd(tmp1, subtrahend_reg);

            code.pshufd(unpack_reg, tmp1, 0b01001110);
            code.addpd(unpack_reg, tmp1);

            code.unpcklpd(xmm, unpack_reg);
        }
    }

    if (ctx.FPSCR_RMode() == FP::RoundingMode::TowardsMinusInfinity) {
        code.pand(xmm, code.MConst(xword, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF));
    }

    ctx.reg_alloc.DefineValue(inst, xmm);
}

} // namespace Dynarmic::BackendX64
