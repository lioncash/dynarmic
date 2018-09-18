/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

#include "backend/x64/abi.h"
#include "backend/x64/block_of_code.h"
#include "backend/x64/emit_x64.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/fp/fpcr.h"
#include "common/fp/info.h"
#include "common/fp/op.h"
#include "common/fp/util.h"
#include "common/mp/cartesian_product.h"
#include "common/mp/function_info.h"
#include "common/mp/integer.h"
#include "common/mp/list.h"
#include "common/mp/lut.h"
#include "common/mp/to_tuple.h"
#include "common/mp/vlift.h"
#include "common/mp/vllift.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;
namespace mp = Common::mp;

namespace {

template<size_t fsize, typename T>
T ChooseOnFsize([[maybe_unused]] T f32, [[maybe_unused]] T f64) {
    static_assert(fsize == 32 || fsize == 64, "fsize must be either 32 or 64");

    if constexpr (fsize == 32) {
        return f32;
    } else {
        return f64;
    }
}

#define FCODE(NAME) (code.*ChooseOnFsize<fsize>(&Xbyak::CodeGenerator::NAME##s, &Xbyak::CodeGenerator::NAME##d))

template<size_t fsize, template<typename> class Indexer, size_t narg>
struct NaNHandler {
public:
    using FPT = mp::unsigned_integer_of_size<fsize>;

    using function_type = void(*)(std::array<VectorArray<FPT>, narg>&, FP::FPCR);

    static function_type GetDefault() {
        return GetDefaultImpl(std::make_index_sequence<narg - 1>{});
    }

private:
    template<size_t... argi>
    static function_type GetDefaultImpl(std::index_sequence<argi...>) {
        const auto result = [](std::array<VectorArray<FPT>, narg>& values, FP::FPCR) {
            VectorArray<FPT>& result = values[0];
            for (size_t elementi = 0; elementi < result.size(); ++elementi) {
                const auto current_values = Indexer<FPT>{}(elementi, values[argi + 1]...);
                if (auto r = FP::ProcessNaNs(std::get<argi>(current_values)...)) {
                    result[elementi] = *r;
                } else if (FP::IsNaN(result[elementi])) {
                    result[elementi] = FP::FPInfo<FPT>::DefaultNaN();
                }
            }
        };

        return static_cast<function_type>(result);
    }
};

template<size_t fsize, size_t nargs, typename NaNHandler>
void HandleNaNs(BlockOfCode& code, EmitContext& ctx, std::array<Xbyak::Xmm, nargs + 1> xmms, const Xbyak::Xmm& nan_mask, NaNHandler nan_handler) {
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

    code.jnz(nan, code.T_NEAR);
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
    code.mov(code.ABI_PARAM2, ctx.FPCR());

    code.CallFunction(nan_handler);

    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.add(rsp, stack_space + ABI_SHADOW_SPACE);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
    code.add(rsp, 8);
    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

template<size_t fsize>
Xbyak::Address GetVectorOf(BlockOfCode& code, u64 value) {
    if constexpr (fsize == 32) {
        return code.MConst(xword, (value << 32) | value, (value << 32) | value);
    } else {
        return code.MConst(xword, value, value);
    }
}

template<size_t fsize, u64 value>
Xbyak::Address GetVectorOf(BlockOfCode& code) {
    if constexpr (fsize == 32) {
        return code.MConst(xword, (value << 32) | value, (value << 32) | value);
    } else {
        return code.MConst(xword, value, value);
    }
}

template<size_t fsize>
Xbyak::Address GetNaNVector(BlockOfCode& code) {
    using FPT = mp::unsigned_integer_of_size<fsize>;
    return GetVectorOf<fsize, FP::FPInfo<FPT>::DefaultNaN()>(code);
}

template<size_t fsize>
Xbyak::Address GetNegativeZeroVector(BlockOfCode& code) {
    using FPT = mp::unsigned_integer_of_size<fsize>;
    return GetVectorOf<fsize, FP::FPInfo<FPT>::Zero(true)>(code);
}

template<size_t fsize>
Xbyak::Address GetSmallestNormalVector(BlockOfCode& code) {
    using FPT = mp::unsigned_integer_of_size<fsize>;
    constexpr FPT smallest_normal_number = FP::FPValue<FPT, false, FP::FPInfo<FPT>::exponent_min, 1>();
    return GetVectorOf<fsize, smallest_normal_number>(code);
}

template<size_t fsize, bool sign, int exponent, mp::unsigned_integer_of_size<fsize> value>
Xbyak::Address GetVectorOf(BlockOfCode& code) {
    using FPT = mp::unsigned_integer_of_size<fsize>;
    return GetVectorOf<fsize, FP::FPValue<FPT, sign, exponent, value>()>(code);
}

template<size_t fsize>
void ForceToDefaultNaN(BlockOfCode& code, EmitContext& ctx, Xbyak::Xmm result) {
    if (ctx.FPSCR_DN()) {
        const Xbyak::Xmm nan_mask = xmm0;
        if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
            FCODE(vcmpunordp)(nan_mask, result, result);
            FCODE(blendvp)(result, GetNaNVector<fsize>(code));
        } else {
            code.movaps(nan_mask, result);
            FCODE(cmpordp)(nan_mask, nan_mask);
            code.andps(result, nan_mask);
            code.andnps(nan_mask, GetNaNVector<fsize>(code));
            code.orps(result, nan_mask);
        }
    }
}

template<size_t fsize>
void DenormalsAreZero(BlockOfCode& code, EmitContext& ctx, std::initializer_list<Xbyak::Xmm> to_daz, Xbyak::Xmm tmp) {
    if (ctx.FPSCR_FTZ()) {
        if (ctx.FPSCR_RMode() != FP::RoundingMode::TowardsMinusInfinity) {
            code.movaps(tmp, GetNegativeZeroVector<fsize>(code));
        } else {
            code.xorps(tmp, tmp);
        }
        for (const Xbyak::Xmm& xmm : to_daz) {
            FCODE(addp)(xmm, tmp);
        }
    }
}

template<typename T>
struct DefaultIndexer {
    std::tuple<T> operator()(size_t i, const VectorArray<T>& a) {
        return std::make_tuple(a[i]);
    }

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
void EmitTwoOpVectorOperation(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn, typename NaNHandler<fsize, Indexer, 2>::function_type nan_handler = NaNHandler<fsize, Indexer, 2>::GetDefault()) {
    static_assert(fsize == 32 || fsize == 64, "fsize must be either 32 or 64");

    if (!ctx.AccurateNaN() || ctx.FPSCR_DN()) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        Xbyak::Xmm result;

        if constexpr (std::is_member_function_pointer_v<Function>) {
            result = ctx.reg_alloc.UseScratchXmm(args[0]);
            (code.*fn)(result);
        } else {
            const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseXmm(args[0]);
            result = ctx.reg_alloc.ScratchXmm();
            fn(result, xmm_a);
        }

        ForceToDefaultNaN<fsize>(code, ctx, result);

        ctx.reg_alloc.DefineValue(inst, result);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();

    if constexpr (std::is_member_function_pointer_v<Function>) {
        code.movaps(result, xmm_a);
        (code.*fn)(result);
    } else {
        fn(result, xmm_a);
    }

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
        FCODE(vcmpunordp)(nan_mask, result, result);
    } else {
        code.movaps(nan_mask, result);
        FCODE(cmpunordp)(nan_mask, nan_mask);
    }

    HandleNaNs<fsize, 1>(code, ctx, {result, xmm_a}, nan_mask, nan_handler);

    ctx.reg_alloc.DefineValue(inst, result);
}

template<size_t fsize, template<typename> class Indexer, typename Function>
void EmitThreeOpVectorOperation(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Function fn, typename NaNHandler<fsize, Indexer, 3>::function_type nan_handler = NaNHandler<fsize, Indexer, 3>::GetDefault()) {
    static_assert(fsize == 32 || fsize == 64, "fsize must be either 32 or 64");

    if (!ctx.AccurateNaN() || ctx.FPSCR_DN()) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
        const Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

        if constexpr (std::is_member_function_pointer_v<Function>) {
            (code.*fn)(xmm_a, xmm_b);
        } else {
            fn(xmm_a, xmm_b);
        }

        ForceToDefaultNaN<fsize>(code, ctx, xmm_a);

        ctx.reg_alloc.DefineValue(inst, xmm_a);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
    const Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();

    code.movaps(nan_mask, xmm_b);
    code.movaps(result, xmm_a);
    FCODE(cmpunordp)(nan_mask, xmm_a);
    if constexpr (std::is_member_function_pointer_v<Function>) {
        (code.*fn)(result, xmm_b);
    } else {
        fn(result, xmm_b);
    }
    FCODE(cmpunordp)(nan_mask, result);

    HandleNaNs<fsize, 2>(code, ctx, {result, xmm_a, xmm_b}, nan_mask, nan_handler);

    ctx.reg_alloc.DefineValue(inst, result);
}

template<typename Lambda>
void EmitTwoOpFallback(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    const auto fn = static_cast<mp::equivalent_function_type_t<Lambda>*>(lambda);
    
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm arg1 = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
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
    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);

    ctx.reg_alloc.DefineValue(inst, result);
}

template<typename Lambda>
void EmitThreeOpFallbackWithoutRegAlloc(BlockOfCode& code, EmitContext& ctx, Xbyak::Xmm result, Xbyak::Xmm arg1, Xbyak::Xmm arg2, Lambda lambda) {
    const auto fn = static_cast<mp::equivalent_function_type_t<Lambda>*>(lambda);

#ifdef _WIN32
    constexpr u32 stack_space = 4 * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 2 * 16]);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE + 3 * 16]);
    code.mov(code.ABI_PARAM4.cvt32(), ctx.FPCR());
    code.lea(rax, code.ptr[code.r15 + code.GetJitStateInfo().offsetof_fpsr_exc]);
    code.mov(qword[rsp + ABI_SHADOW_SPACE + 0], rax);
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
    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 1 * 16]);
#else
    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);
#endif

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);
}

template<typename Lambda>
void EmitThreeOpFallback(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm arg1 = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm arg2 = ctx.reg_alloc.UseXmm(args[1]);
    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    ctx.reg_alloc.EndOfAllocScope();
    ctx.reg_alloc.HostCall(nullptr);

    EmitThreeOpFallbackWithoutRegAlloc(code, ctx, result, arg1, arg2, lambda);

    ctx.reg_alloc.DefineValue(inst, result);
}

template<typename Lambda>
void EmitFourOpFallbackWithoutRegAlloc(BlockOfCode& code, EmitContext& ctx, Xbyak::Xmm result, Xbyak::Xmm arg1, Xbyak::Xmm arg2, Xbyak::Xmm arg3, Lambda lambda) {
    const auto fn = static_cast<mp::equivalent_function_type_t<Lambda>*>(lambda);

#ifdef _WIN32
    constexpr u32 stack_space = 5 * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 2 * 16]);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE + 3 * 16]);
    code.lea(code.ABI_PARAM4, ptr[rsp + ABI_SHADOW_SPACE + 4 * 16]);
    code.mov(qword[rsp + ABI_SHADOW_SPACE + 0], ctx.FPCR());
    code.lea(rax, code.ptr[code.r15 + code.GetJitStateInfo().offsetof_fpsr_exc]);
    code.mov(qword[rsp + ABI_SHADOW_SPACE + 8], rax);
#else
    constexpr u32 stack_space = 4 * 16;
    code.sub(rsp, stack_space + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM1, ptr[rsp + ABI_SHADOW_SPACE + 0 * 16]);
    code.lea(code.ABI_PARAM2, ptr[rsp + ABI_SHADOW_SPACE + 1 * 16]);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE + 2 * 16]);
    code.lea(code.ABI_PARAM4, ptr[rsp + ABI_SHADOW_SPACE + 3 * 16]);
    code.mov(code.ABI_PARAM5.cvt32(), ctx.FPCR());
    code.lea(code.ABI_PARAM6, code.ptr[code.r15 + code.GetJitStateInfo().offsetof_fpsr_exc]);
#endif

    code.movaps(xword[code.ABI_PARAM2], arg1);
    code.movaps(xword[code.ABI_PARAM3], arg2);
    code.movaps(xword[code.ABI_PARAM4], arg3);
    code.CallFunction(fn);

#ifdef _WIN32
    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 1 * 16]);
#else
    code.movaps(result, xword[rsp + ABI_SHADOW_SPACE + 0 * 16]);
#endif

    code.add(rsp, stack_space + ABI_SHADOW_SPACE);
}

template<typename Lambda>
void EmitFourOpFallback(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, Lambda lambda) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm arg1 = ctx.reg_alloc.UseXmm(args[0]);
    const Xbyak::Xmm arg2 = ctx.reg_alloc.UseXmm(args[1]);
    const Xbyak::Xmm arg3 = ctx.reg_alloc.UseXmm(args[2]);
    const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    ctx.reg_alloc.EndOfAllocScope();
    ctx.reg_alloc.HostCall(nullptr);

    EmitFourOpFallbackWithoutRegAlloc(code, ctx, result, arg1, arg2, arg3, lambda);

    ctx.reg_alloc.DefineValue(inst, result);
}

} // anonymous namespace

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

void EmitX64::EmitFPVectorFromSignedFixed32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);
    const int fbits = args[1].GetImmediateU8();
    const FP::RoundingMode rounding_mode = static_cast<FP::RoundingMode>(args[2].GetImmediateU8());
    ASSERT(rounding_mode == ctx.FPSCR_RMode());

    code.cvtdq2ps(xmm, xmm);

    if (fbits != 0) {
        code.mulps(xmm, GetVectorOf<32>(code, static_cast<u32>(127 - fbits) << 23));
    }

    ctx.reg_alloc.DefineValue(inst, xmm);
}

void EmitX64::EmitFPVectorFromSignedFixed64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);
    const int fbits = args[1].GetImmediateU8();
    const FP::RoundingMode rounding_mode = static_cast<FP::RoundingMode>(args[2].GetImmediateU8());
    ASSERT(rounding_mode == ctx.FPSCR_RMode());

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

    if (fbits != 0) {
        code.mulpd(xmm, GetVectorOf<64>(code, static_cast<u64>(1023 - fbits) << 52));
    }

    ctx.reg_alloc.DefineValue(inst, xmm);
}

void EmitX64::EmitFPVectorFromUnsignedFixed32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);
    const int fbits = args[1].GetImmediateU8();
    const FP::RoundingMode rounding_mode = static_cast<FP::RoundingMode>(args[2].GetImmediateU8());
    ASSERT(rounding_mode == ctx.FPSCR_RMode());

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

    if (fbits != 0) {
        code.mulps(xmm, GetVectorOf<32>(code, static_cast<u32>(127 - fbits) << 23));
    }

    if (ctx.FPSCR_RMode() == FP::RoundingMode::TowardsMinusInfinity) {
        code.pand(xmm, code.MConst(xword, 0x7FFFFFFF7FFFFFFF, 0x7FFFFFFF7FFFFFFF));
    }

    ctx.reg_alloc.DefineValue(inst, xmm);
}

void EmitX64::EmitFPVectorFromUnsignedFixed64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const Xbyak::Xmm xmm = ctx.reg_alloc.UseScratchXmm(args[0]);
    const int fbits = args[1].GetImmediateU8();
    const FP::RoundingMode rounding_mode = static_cast<FP::RoundingMode>(args[2].GetImmediateU8());
    ASSERT(rounding_mode == ctx.FPSCR_RMode());

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

    if (fbits != 0) {
        code.mulpd(xmm, GetVectorOf<64>(code, static_cast<u64>(1023 - fbits) << 52));
    }

    if (ctx.FPSCR_RMode() == FP::RoundingMode::TowardsMinusInfinity) {
        code.pand(xmm, code.MConst(xword, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF));
    }

    ctx.reg_alloc.DefineValue(inst, xmm);
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

template<size_t fsize, bool is_max>
static void EmitFPVectorMinMax(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    if (ctx.FPSCR_DN()) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        const Xbyak::Xmm result = ctx.reg_alloc.UseScratchXmm(args[0]);
        const Xbyak::Xmm xmm_b = ctx.FPSCR_FTZ() ? ctx.reg_alloc.UseScratchXmm(args[1]) : ctx.reg_alloc.UseXmm(args[1]);

        const Xbyak::Xmm mask = xmm0;
        const Xbyak::Xmm eq = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm nan_mask = ctx.reg_alloc.ScratchXmm();

        DenormalsAreZero<fsize>(code, ctx, {result, xmm_b}, mask);

        if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
            FCODE(vcmpeqp)(mask, result, xmm_b);
            FCODE(vcmpunordp)(nan_mask, result, xmm_b);
            if constexpr (is_max) {
                FCODE(vandp)(eq, result, xmm_b);
                FCODE(vmaxp)(result, result, xmm_b);
            } else {
                FCODE(vorp)(eq, result, xmm_b);
                FCODE(vminp)(result, result, xmm_b);
            }
            FCODE(blendvp)(result, eq);
            FCODE(vblendvp)(result, result, GetNaNVector<fsize>(code), nan_mask);
        } else {
            code.movaps(mask, result);
            code.movaps(eq, result);
            code.movaps(nan_mask, result);
            FCODE(cmpneqp)(mask, xmm_b);
            FCODE(cmpordp)(nan_mask, xmm_b);

            if constexpr (is_max) {
                code.andps(eq, xmm_b);
                FCODE(maxp)(result, xmm_b);
            } else {
                code.orps(eq, xmm_b);
                FCODE(minp)(result, xmm_b);
            }

            code.andps(result, mask);
            code.andnps(mask, eq);
            code.orps(result, mask);

            code.andps(result, nan_mask);
            code.andnps(nan_mask, GetNaNVector<fsize>(code));
            code.orps(result, nan_mask);
        }

        ctx.reg_alloc.DefineValue(inst, result);

        return;
    }

    EmitThreeOpVectorOperation<fsize, DefaultIndexer>(code, ctx, inst, [&](const Xbyak::Xmm& result, Xbyak::Xmm xmm_b){
        const Xbyak::Xmm mask = xmm0;
        const Xbyak::Xmm eq = ctx.reg_alloc.ScratchXmm();

        if (ctx.FPSCR_FTZ()) {
            const Xbyak::Xmm prev_xmm_b = xmm_b;
            xmm_b = ctx.reg_alloc.ScratchXmm();
            code.movaps(xmm_b, prev_xmm_b);
            DenormalsAreZero<fsize>(code, ctx, {result, xmm_b}, mask);
        }

        // What we are doing here is handling the case when the inputs are differently signed zeros.
        // x86-64 treats differently signed zeros as equal while ARM does not.
        // Thus if we AND together things that x86-64 thinks are equal we'll get the positive zero.

        if (code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
            FCODE(vcmpeqp)(mask, result, xmm_b);
            if constexpr (is_max) {
                FCODE(vandp)(eq, result, xmm_b);
                FCODE(vmaxp)(result, result, xmm_b);
            } else {
                FCODE(vorp)(eq, result, xmm_b);
                FCODE(vminp)(result, result, xmm_b);
            }
            FCODE(blendvp)(result, eq);
        } else {
            code.movaps(mask, result);
            code.movaps(eq, result);
            FCODE(cmpneqp)(mask, xmm_b);

            if constexpr (is_max) {
                code.andps(eq, xmm_b);
                FCODE(maxp)(result, xmm_b);
            } else {
                code.orps(eq, xmm_b);
                FCODE(minp)(result, xmm_b);
            }

            code.andps(result, mask);
            code.andnps(mask, eq);
            code.orps(result, mask);
        }
    });
}

void EmitX64::EmitFPVectorMax32(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorMinMax<32, true>(code, ctx, inst);
}

void EmitX64::EmitFPVectorMax64(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorMinMax<64, true>(code, ctx, inst);
}

void EmitX64::EmitFPVectorMin32(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorMinMax<32, false>(code, ctx, inst);
}

void EmitX64::EmitFPVectorMin64(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorMinMax<64, false>(code, ctx, inst);
}

void EmitX64::EmitFPVectorMul32(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<32, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::mulps);
}

void EmitX64::EmitFPVectorMul64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::mulpd);
}

template<size_t fsize>
void EmitFPVectorMulAdd(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    using FPT = mp::unsigned_integer_of_size<fsize>;

    const auto fallback_fn = [](VectorArray<FPT>& result, const VectorArray<FPT>& addend, const VectorArray<FPT>& op1, const VectorArray<FPT>& op2, FP::FPCR fpcr, FP::FPSR& fpsr) {
        for (size_t i = 0; i < result.size(); i++) {
            result[i] = FP::FPMulAdd<FPT>(addend[i], op1[i], op2[i], fpcr, fpsr);
        }
    };

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tFMA) && code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm xmm_a = ctx.reg_alloc.UseXmm(args[0]);
        const Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);
        const Xbyak::Xmm xmm_c = ctx.reg_alloc.UseXmm(args[2]);
        const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        Xbyak::Label end, fallback;

        code.movaps(result, xmm_a);
        FCODE(vfmadd231p)(result, xmm_b, xmm_c);

        code.movaps(tmp, GetNegativeZeroVector<fsize>(code));
        code.andnps(tmp, result);
        FCODE(vcmpeq_uqp)(tmp, tmp, GetSmallestNormalVector<fsize>(code));
        code.vptest(tmp, tmp);
        code.jnz(fallback, code.T_NEAR);
        code.L(end);

        code.SwitchToFarCode();
        code.L(fallback);
        code.sub(rsp, 8);
        ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
        EmitFourOpFallbackWithoutRegAlloc(code, ctx, result, xmm_a, xmm_b, xmm_c, fallback_fn);
        ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
        code.add(rsp, 8);
        code.jmp(end, code.T_NEAR);
        code.SwitchToNearCode();

        ctx.reg_alloc.DefineValue(inst, result);
        return;
    }

    EmitFourOpFallback(code, ctx, inst, fallback_fn);
}

void EmitX64::EmitFPVectorMulAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorMulAdd<32>(code, ctx, inst);
}

void EmitX64::EmitFPVectorMulAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorMulAdd<64>(code, ctx, inst);
}

void EmitX64::EmitFPVectorNeg16(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Address mask = code.MConst(xword, 0x8000800080008000, 0x8000800080008000);

    code.pxor(a, mask);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorNeg32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Address mask = code.MConst(xword, 0x8000000080000000, 0x8000000080000000);

    code.pxor(a, mask);

    ctx.reg_alloc.DefineValue(inst, a);
}

void EmitX64::EmitFPVectorNeg64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const Xbyak::Xmm a = ctx.reg_alloc.UseScratchXmm(args[0]);
    const Xbyak::Address mask = code.MConst(xword, 0x8000000000000000, 0x8000000000000000);

    code.pxor(a, mask);

    ctx.reg_alloc.DefineValue(inst, a);
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
static void EmitRecipEstimate(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    EmitTwoOpFallback(code, ctx, inst, [](VectorArray<FPT>& result, const VectorArray<FPT>& operand, FP::FPCR fpcr, FP::FPSR& fpsr) {
        for (size_t i = 0; i < result.size(); i++) {
            result[i] = FP::FPRecipEstimate<FPT>(operand[i], fpcr, fpsr);
        }
    });
}

void EmitX64::EmitFPVectorRecipEstimate32(EmitContext& ctx, IR::Inst* inst) {
    EmitRecipEstimate<u32>(code, ctx, inst);
}

void EmitX64::EmitFPVectorRecipEstimate64(EmitContext& ctx, IR::Inst* inst) {
    EmitRecipEstimate<u64>(code, ctx, inst);
}

template<size_t fsize>
static void EmitRecipStepFused(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    using FPT = mp::unsigned_integer_of_size<fsize>;

    const auto fallback_fn = [](VectorArray<FPT>& result, const VectorArray<FPT>& op1, const VectorArray<FPT>& op2, FP::FPCR fpcr, FP::FPSR& fpsr) {
        for (size_t i = 0; i < result.size(); i++) {
            result[i] = FP::FPRecipStepFused<FPT>(op1[i], op2[i], fpcr, fpsr);
        }
    };

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tFMA) && code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm operand1 = ctx.reg_alloc.UseXmm(args[0]);
        const Xbyak::Xmm operand2 = ctx.reg_alloc.UseXmm(args[1]);
        const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();

        Xbyak::Label end, fallback;

        code.movaps(result, GetVectorOf<fsize, false, 0, 2>(code));
        FCODE(vfnmadd231p)(result, operand1, operand2);

        FCODE(vcmpunordp)(tmp, result, result);
        code.vptest(tmp, tmp);
        code.jnz(fallback, code.T_NEAR);
        code.L(end);

        code.SwitchToFarCode();
        code.L(fallback);
        code.sub(rsp, 8);
        ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
        EmitThreeOpFallbackWithoutRegAlloc(code, ctx, result, operand1, operand2, fallback_fn);
        ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
        code.add(rsp, 8);
        code.jmp(end, code.T_NEAR);
        code.SwitchToNearCode();

        ctx.reg_alloc.DefineValue(inst, result);
        return;
    }

    EmitThreeOpFallback(code, ctx, inst, fallback_fn);
}

void EmitX64::EmitFPVectorRecipStepFused32(EmitContext& ctx, IR::Inst* inst) {
    EmitRecipStepFused<32>(code, ctx, inst);
}

void EmitX64::EmitFPVectorRecipStepFused64(EmitContext& ctx, IR::Inst* inst) {
    EmitRecipStepFused<64>(code, ctx, inst);
}

template<size_t fsize>
void EmitFPVectorRoundInt(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    using FPT = mp::unsigned_integer_of_size<fsize>;

    const auto rounding = static_cast<FP::RoundingMode>(inst->GetArg(1).GetU8());
    const bool exact = inst->GetArg(2).GetU1();

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41) && rounding != FP::RoundingMode::ToNearest_TieAwayFromZero && !exact) {
        const u8 round_imm = [&]() -> u8 {
            switch (rounding) {
            case FP::RoundingMode::ToNearest_TieEven:
                return 0b00;
            case FP::RoundingMode::TowardsPlusInfinity:
                return 0b10;
            case FP::RoundingMode::TowardsMinusInfinity:
                return 0b01;
            case FP::RoundingMode::TowardsZero:
                return 0b11;
            default:
                UNREACHABLE();
            }
            return 0;
        }();

        EmitTwoOpVectorOperation<fsize, DefaultIndexer>(code, ctx, inst, [&](const Xbyak::Xmm& result, const Xbyak::Xmm& xmm_a){
            if constexpr (fsize == 32) {
                code.roundps(result, xmm_a, round_imm);
            } else {
                code.roundpd(result, xmm_a, round_imm);
            }
        });

        return;
    }

    using rounding_list = mp::list<
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::ToNearest_TieEven>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::TowardsPlusInfinity>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::TowardsMinusInfinity>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::TowardsZero>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::ToNearest_TieAwayFromZero>
    >;
    using exact_list = mp::list<mp::vlift<true>, mp::vlift<false>>;

    using key_type = std::tuple<FP::RoundingMode, bool>;
    using value_type = void(*)(VectorArray<FPT>&, const VectorArray<FPT>&, FP::FPCR, FP::FPSR&);

    static const auto lut = mp::GenerateLookupTableFromList<key_type, value_type>(
        [](auto arg) {
            return std::pair<key_type, value_type>{
                mp::to_tuple<decltype(arg)>,
                static_cast<value_type>(
                    [](VectorArray<FPT>& output, const VectorArray<FPT>& input, FP::FPCR fpcr, FP::FPSR& fpsr) {
                        constexpr FP::RoundingMode rounding_mode = std::get<0>(mp::to_tuple<decltype(arg)>);
                        constexpr bool exact = std::get<1>(mp::to_tuple<decltype(arg)>);

                        for (size_t i = 0; i < output.size(); ++i) {
                            output[i] = static_cast<FPT>(FP::FPRoundInt<FPT>(input[i], fpcr, rounding_mode, exact, fpsr));
                        }
                    }
                )
            };
        },
        mp::cartesian_product<rounding_list, exact_list>{}
    );

    EmitTwoOpFallback(code, ctx, inst, lut.at(std::make_tuple(rounding, exact)));
}

void EmitX64::EmitFPVectorRoundInt32(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorRoundInt<32>(code, ctx, inst);
}

void EmitX64::EmitFPVectorRoundInt64(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorRoundInt<64>(code, ctx, inst);
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

template<size_t fsize>
static void EmitRSqrtStepFused(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    using FPT = mp::unsigned_integer_of_size<fsize>;

    const auto fallback_fn = [](VectorArray<FPT>& result, const VectorArray<FPT>& op1, const VectorArray<FPT>& op2, FP::FPCR fpcr, FP::FPSR& fpsr) {
        for (size_t i = 0; i < result.size(); i++) {
            result[i] = FP::FPRSqrtStepFused<FPT>(op1[i], op2[i], fpcr, fpsr);
        }
    };

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tFMA) && code.DoesCpuSupport(Xbyak::util::Cpu::tAVX)) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        const Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm operand1 = ctx.reg_alloc.UseXmm(args[0]);
        const Xbyak::Xmm operand2 = ctx.reg_alloc.UseXmm(args[1]);
        const Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
        const Xbyak::Xmm mask = ctx.reg_alloc.ScratchXmm();

        Xbyak::Label end, fallback;

        code.vmovaps(result, GetVectorOf<fsize, false, 0, 3>(code));
        FCODE(vfnmadd231p)(result, operand1, operand2);

        // An explanation for this is given in EmitFPRSqrtStepFused.
        code.vmovaps(mask, GetVectorOf<fsize, fsize == 32 ? 0x7f000000 : 0x7fe0000000000000>(code));
        FCODE(vandp)(tmp, result, mask);
        if constexpr (fsize == 32) {
            code.vpcmpeqd(tmp, tmp, mask);
        } else {
            code.vpcmpeqq(tmp, tmp, mask);
        }
        code.ptest(tmp, tmp);
        code.jnz(fallback, code.T_NEAR);

        FCODE(vmulp)(result, result, GetVectorOf<fsize, false, -1, 1>(code));
        code.L(end);

        code.SwitchToFarCode();
        code.L(fallback);
        code.sub(rsp, 8);
        ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
        EmitThreeOpFallbackWithoutRegAlloc(code, ctx, result, operand1, operand2, fallback_fn);
        ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(result.getIdx()));
        code.add(rsp, 8);
        code.jmp(end, code.T_NEAR);
        code.SwitchToNearCode();

        ctx.reg_alloc.DefineValue(inst, result);
        return;
    }

    EmitThreeOpFallback(code, ctx, inst, fallback_fn);
}

void EmitX64::EmitFPVectorRSqrtStepFused32(EmitContext& ctx, IR::Inst* inst) {
    EmitRSqrtStepFused<32>(code, ctx, inst);
}

void EmitX64::EmitFPVectorRSqrtStepFused64(EmitContext& ctx, IR::Inst* inst) {
    EmitRSqrtStepFused<64>(code, ctx, inst);
}

void EmitX64::EmitFPVectorSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<32, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::subps);
}

void EmitX64::EmitFPVectorSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitThreeOpVectorOperation<64, DefaultIndexer>(code, ctx, inst, &Xbyak::CodeGenerator::subpd);
}

template<size_t fsize, bool unsigned_>
void EmitFPVectorToFixed(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    using FPT = mp::unsigned_integer_of_size<fsize>;

    const size_t fbits = inst->GetArg(1).GetU8();
    const auto rounding = static_cast<FP::RoundingMode>(inst->GetArg(2).GetU8());

    using fbits_list = mp::vllift<std::make_index_sequence<fsize>>;
    using rounding_list = mp::list<
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::ToNearest_TieEven>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::TowardsPlusInfinity>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::TowardsMinusInfinity>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::TowardsZero>,
        std::integral_constant<FP::RoundingMode, FP::RoundingMode::ToNearest_TieAwayFromZero>
    >;

    using key_type = std::tuple<size_t, FP::RoundingMode>;
    using value_type = void(*)(VectorArray<FPT>&, const VectorArray<FPT>&, FP::FPCR, FP::FPSR&);

    static const auto lut = mp::GenerateLookupTableFromList<key_type, value_type>(
        [](auto arg) {
            return std::pair<key_type, value_type>{
                mp::to_tuple<decltype(arg)>,
                static_cast<value_type>(
                    [](VectorArray<FPT>& output, const VectorArray<FPT>& input, FP::FPCR fpcr, FP::FPSR& fpsr) {
                        constexpr size_t fbits = std::get<0>(mp::to_tuple<decltype(arg)>);
                        constexpr FP::RoundingMode rounding_mode = std::get<1>(mp::to_tuple<decltype(arg)>);

                        for (size_t i = 0; i < output.size(); ++i) {
                            output[i] = static_cast<FPT>(FP::FPToFixed<FPT>(fsize, input[i], fbits, unsigned_, fpcr, rounding_mode, fpsr));
                        }
                    }
                )
            };
        },
        mp::cartesian_product<fbits_list, rounding_list>{}
    );

    EmitTwoOpFallback(code, ctx, inst, lut.at(std::make_tuple(fbits, rounding)));
}

void EmitX64::EmitFPVectorToSignedFixed32(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorToFixed<32, false>(code, ctx, inst);
}

void EmitX64::EmitFPVectorToSignedFixed64(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorToFixed<64, false>(code, ctx, inst);
}

void EmitX64::EmitFPVectorToUnsignedFixed32(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorToFixed<32, true>(code, ctx, inst);
}

void EmitX64::EmitFPVectorToUnsignedFixed64(EmitContext& ctx, IR::Inst* inst) {
    EmitFPVectorToFixed<64, true>(code, ctx, inst);
}

} // namespace Dynarmic::BackendX64
