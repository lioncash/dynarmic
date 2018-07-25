/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/fp/fpcr.h"
#include "common/fp/info.h"
#include "common/fp/op.h"
#include "common/fp/util.h"
#include "common/mp/function_info.h"
#include "common/mp/integer.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;
namespace mp = Common::mp;

template<size_t fsize, typename T>
static T ChooseOnFsize(T f32, T f64) {
    static_assert(fsize == 32 || fsize == 64, "fsize must be either 32 or 64");

    if constexpr (fsize == 32) {
        return f32;
    } else {
        return f64;
    }
}

#define FCODE(NAME) (code.*ChooseOnFsize<fsize>(&Xbyak::CodeGenerator::NAME##s, &Xbyak::CodeGenerator::NAME##d))

template<typename T, template<typename> class Indexer, size_t... argi>
static auto GetRuntimeNaNFunction(std::index_sequence<argi...>) {
    auto result = [](std::array<VectorArray<T>, sizeof...(argi) + 1>& values) {
        VectorArray<T>& result = values[0];
        for (size_t elementi = 0; elementi < result.size(); ++elementi) {
            const auto current_values = Indexer<T>{}(elementi, values[argi + 1]...);
            if (auto r = FP::ProcessNaNs(std::get<argi>(current_values)...)) {
                result[elementi] = *r;
            } else if (FP::IsNaN(result[elementi])) {
                result[elementi] = FP::FPInfo<T>::DefaultNaN();
            }
        }
    };
    return static_cast<mp::equivalent_function_type_t<decltype(result)>*>(result);
}

template<size_t fsize, size_t nargs, template<typename> class Indexer>
static void HandleNaNs(BlockOfCode& code, EmitContext& ctx, std::array<Xbyak::Xmm, nargs + 1> xmms, const Xbyak::Xmm& nan_mask) {
    static_assert(fsize == 32 || fsize == 64, "fsize must be either 32 or 64");

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

    const Xbyak::Xmm result = xmms[0];

    code.sub(rsp, 8);
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));

    const size_t stack_space = xmms.size() * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    for (size_t i = 0; i < xmms.size(); ++i) {
        code.movaps(xword[rsp + ABI_SHADOW_SPACE + i * 16], xmms[i]);
    }
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 0 * 16]);

    using T = mp::unsigned_integer_of_size<fsize>;
    code.CallFunction(GetRuntimeNaNFunction<T, Indexer>(std::make_index_sequence<nargs>{}));

    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.add(rsp, stack_space + ABI_SHADOW_SPACE);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
    code.add(rsp, 8);
    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

template<typename T>
struct DefaultIndexer {
    std::tuple<T, T> operator()(size_t i, const VectorArray<T>& a, const VectorArray<T>& b) {
        return std::make_tuple(a[i], b[i]);
    }

    std::tuple<T, T, T> operator()(size_t i, const VectorArray<T>& a, const VectorArray<T>& b, const VectorArray<T>& c) {
        return std::make_tuple(a[i], b[i], c[i]);
    }
};

template<typename T>
struct PairedIndexer {
    std::tuple<T, T> operator()(size_t i, const VectorArray<T>& a, const VectorArray<T>& b) {
        constexpr size_t halfway = std::tuple_size_v<VectorArray<T>> / 2;
        const size_t which_array = i / halfway;
        i %= halfway;
        switch (which_array) {
        case 0:
            return std::make_tuple(a[2 * i], a[2 * i + 1]);
        case 1:
            return std::make_tuple(b[2 * i], b[2 * i + 1]);
        }
        UNREACHABLE();
        return {};
    }
};

template<typename T>
struct PairedLowerIndexer {
    std::tuple<T, T> operator()(size_t i, const VectorArray<T>& a, const VectorArray<T>& b) {
        constexpr size_t array_size = std::tuple_size_v<VectorArray<T>>;
        if constexpr (array_size == 4) {
            switch (i) {
            case 0:
                return std::make_tuple(a[0], a[1]);
            case 1:
                return std::make_tuple(b[0], b[1]);
            default:
                return std::make_tuple(0, 0);
            }
        } else if constexpr (array_size == 2) {
            if (i == 0) {
                return std::make_tuple(a[0], b[0]);
            }
            return std::make_tuple(0, 0);
        }
        UNREACHABLE();
        return {};
    }
};

template<size_t fsize, template<typename> class Indexer, typename Function>
static void EmitThreeOpVectorOperation(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn) {
    static_assert(fsize == 32 || fsize == 64, "fsize must be either 32 or 64");

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
            FCODE(cmpordp)(nan_mask, nan_mask);
            code.andps(xmm_a, nan_mask);
            code.xorps(nan_mask, tmp);
            code.andps(nan_mask, fsize == 32 ? code.MConst(xword, 0x7fc0'0000'7fc0'0000, 0x7fc0'0000'7fc0'0000) : code.MConst(xword, 0x7ff8'0000'0000'0000, 0x7ff8'0000'0000'0000));
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
    FCODE(cmpunordp)(nan_mask, xmm_a);
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, xmm_b);
    } else {
        fn(result, xmm_b);
    }
    FCODE(cmpunordp)(nan_mask, result);

    HandleNaNs<fsize, 2, Indexer>(code, ctx, {result, xmm_a, xmm_b}, nan_mask);

    ctx.reg_alloc.DefineValue(inst, result);
}

template<typename Lambda>
inline void EmitTwoOpFallback(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    const auto fn = static_cast<mp::equivalent_function_type_t<Lambda>*>(lambda);
    
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm arg1 = ctx.reg_alloc.UseXmm(args[0]);
    ctx.reg_alloc.EndOfAllocScope();
    ctx.reg_alloc.HostCall(nullptr);

    constexpr u32 stack_space = 2 * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.mov(code.ABI_PARAM3.cvt32(), ctx.FPCR());
    code.lea(code.ABI_PARAM4, code.ptr[code.r15 + code.GetJitStateInfo().offsetof_fpsr_exc]);

    code.movaps(xword[code.ABI_PARAM2], arg1);
    code.CallFunction(fn);
    code.movaps(xmm0, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);

    ctx.reg_alloc.DefineValue(inst, xmm0);
}

template<typename Lambda>
inline void EmitThreeOpFallback(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    const auto fn = static_cast<mp::equivalent_function_type_t<Lambda>*>(lambda);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm arg1 = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm arg2 = ctx.reg_alloc.UseXmm(args[1]);
    ctx.reg_alloc.EndOfAllocScope();
    ctx.reg_alloc.HostCall(nullptr);

#ifdef _WIN32
    constexpr u32 stack_space = 4 * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 2 * 16]);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE + 3 * 16]);
    code.mov(code.ABI_PARAM4.cvt32(), ctx.FPCR());
    code.lea(rax, code.ptr[code.r15 + code.GetJitStateInfo().offsetof_fpsr_exc]);
    code.mov(qword[rsp + ABI_SHADOW_SPACE + 0 * 16], rax);
#else
    constexpr u32 stack_space = 3 * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE + 2 * 16]);
    code.mov(code.ABI_PARAM4.cvt32(), ctx.FPCR());
    code.lea(code.ABI_PARAM5, code.ptr[code.r15 + code.GetJitStateInfo().offsetof_fpsr_exc]);
#endif

    code.movaps(xword[code.ABI_PARAM2], arg1);
    code.movaps(xword[code.ABI_PARAM3], arg2);
    code.CallFunction(fn);

#ifdef _WIN32
    code.movaps(xmm0, xword[rsp + ABI_SHADOW_SPACE + 1 * 16]);
#else
    code.movaps(xmm0, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);
#endif

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);

    ctx.reg_alloc.DefineValue(inst, xmm0);
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
    EmitThreeOpVectorOperation<32, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::addps);
}

void EmitX64::EmitFPVectorAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::addpd);
}

void EmitX64::EmitFPVectorDiv32(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<32, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::divps);
}

void EmitX64::EmitFPVectorDiv64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::divpd);
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
    EmitThreeOpVectorOperation<32, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::mulps);
}

void EmitX64::EmitFPVectorMul64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::mulpd);
}

void EmitX64::EmitFPVectorPairedAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<32, PairedIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::haddps);
}

void EmitX64::EmitFPVectorPairedAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, PairedIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::haddpd);
}

void EmitX64::EmitFPVectorPairedAddLower32(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<32, PairedLowerIndexer>(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm xmm_b) {
        const Xbyak::Xmm zero = ctx.reg_alloc.ScratchXmm();
        code.xorps(zero, zero);
        code.punpcklqdq(result, xmm_b);
        code.haddps(result, zero);
    });
}

void EmitX64::EmitFPVectorPairedAddLower64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, PairedLowerIndexer>(code, ctx, inst, [&](Xbyak::Xmm result, Xbyak::Xmm xmm_b) {
        const Xbyak::Xmm zero = ctx.reg_alloc.ScratchXmm();
        code.xorps(zero, zero);
        code.punpcklqdq(result, xmm_b); 
        code.haddpd(result, zero);
    });
}

template<typename FPT>
static void EmitRSqrtEstimate(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpFallback(code, ctx, inst, [](VectorArray<FPT>& result, const VectorArray<FPT>& operand, FP::FPCR fpcr, FP::FPSR& fpsr) {
        for (size_t i = 0; i < result.size(); i++) {
            result[i] = FP::FPRSqrtEstimate<FPT>(operand[i], fpcr, fpsr);
        }
    });
}

void EmitX64::EmitFPVectorRSqrtEstimate32(EmitContext& ctx, IR::Inst* inst) {
    EmitRSqrtEstimate<u32>(code, ctx, inst);
}

void EmitX64::EmitFPVectorRSqrtEstimate64(EmitContext& ctx, IR::Inst* inst) {
    EmitRSqrtEstimate<u64>(code, ctx, inst);
}

template<typename FPT>
static void EmitRSqrtStepFused(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpFallback(code, ctx, inst, [](VectorArray<FPT>& result, const VectorArray<FPT>& op1, const VectorArray<FPT>& op2, FP::FPCR fpcr, FP::FPSR& fpsr) {
        for (size_t i = 0; i < result.size(); i++) {
            result[i] = FP::FPRSqrtStepFused<FPT>(op1[i], op2[i], fpcr, fpsr);
        }
    });
}

void EmitX64::EmitFPVectorRSqrtStepFused32(EmitContext& ctx, IR::Inst* inst) {
    EmitRSqrtStepFused<u32>(code, ctx, inst);
}

void EmitX64::EmitFPVectorRSqrtStepFused64(EmitContext& ctx, IR::Inst* inst) {
    EmitRSqrtStepFused<u64>(code, ctx, inst);
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
    EmitThreeOpVectorOperation<32, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::subps);
}

void EmitX64::EmitFPVectorSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::subpd);
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
