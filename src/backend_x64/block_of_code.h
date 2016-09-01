/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <type_traits>
#include <xbyak.h>

#include "backend_x64/jitstate.h"
#include "common/common_types.h"
#include "dynarmic/callbacks.h"

namespace Dynarmic {
namespace BackendX64 {

class BlockOfCode final : public Xbyak::CodeGenerator {
public:
    explicit BlockOfCode(UserCallbacks cb);

    /// Clears this block of code and resets code pointer to beginning.
    void ClearCache();

    /// Runs emulated code for approximately `cycles_to_run` cycles.
    size_t RunCode(JitState* jit_state, CodePtr basic_block, size_t cycles_to_run) const;
    /// Code emitter: Returns to host
    void ReturnFromRunCode(bool MXCSR_switch = true);
    /// Code emitter: Makes guest MXCSR the current MXCSR
    void SwitchMxcsrOnEntry();
    /// Code emitter: Makes saved host MXCSR the current MXCSR
    void SwitchMxcsrOnExit();

    /// Code emitter: Calls the function
    template <typename FunctionPointer>
    void CallFunction(FunctionPointer fn) {
        static_assert(std::is_pointer<FunctionPointer>() && std::is_function<std::remove_pointer_t<FunctionPointer>>(),
                      "Supplied type must be a pointer to a function");

        const u64 address  = reinterpret_cast<u64>(fn);
        const u64 distance = address - (getCurr<u64>() + 5);

        if (distance >= 0x0000000080000000ULL && distance < 0xFFFFFFFF80000000ULL) {
            // Far call
            mov(rax, address);
            call(rax);
        } else {
            call(fn);
        }
    }

    Xbyak::Address MFloatPositiveZero32() {
        return xword[rip + consts.FloatPositiveZero32];
    }
    Xbyak::Address MFloatNegativeZero32() {
        return xword[rip + consts.FloatNegativeZero32];
    }
    Xbyak::Address MFloatNaN32() {
        return xword[rip + consts.FloatNaN32];
    }
    Xbyak::Address MFloatNonSignMask32() {
        return xword[rip + consts.FloatNonSignMask32];
    }
    Xbyak::Address MFloatPositiveZero64() {
        return xword[rip + consts.FloatPositiveZero64];
    }
    Xbyak::Address MFloatNegativeZero64() {
        return xword[rip + consts.FloatNegativeZero64];
    }
    Xbyak::Address MFloatNaN64() {
        return xword[rip + consts.FloatNaN64];
    }
    Xbyak::Address MFloatNonSignMask64() {
        return xword[rip + consts.FloatNonSignMask64];
    }
    Xbyak::Address MFloatPenultimatePositiveDenormal64() {
        return xword[rip + consts.FloatPenultimatePositiveDenormal64];
    }
    Xbyak::Address MFloatMinS32() {
        return xword[rip + consts.FloatMinS32];
    }
    Xbyak::Address MFloatMaxS32() {
        return xword[rip + consts.FloatMaxS32];
    }
    Xbyak::Address MFloatMinU32() {
        return xword[rip + consts.FloatMinU32];
    }
    Xbyak::Address MFloatMaxU32() {
        return xword[rip + consts.FloatMaxU32];
    }

    const void* GetReturnFromRunCodeAddress() const {
        return return_from_run_code;
    }

    const void* GetMemoryReadCallback(size_t bit_size) const {
        switch (bit_size) {
        case 8:
            return read_memory_8;
        case 16:
            return read_memory_16;
        case 32:
            return read_memory_32;
        case 64:
            return read_memory_64;
        default:
            return nullptr;
        }
    }

    const void* GetMemoryWriteCallback(size_t bit_size) const {
        switch (bit_size) {
        case 8:
            return write_memory_8;
        case 16:
            return write_memory_16;
        case 32:
            return write_memory_32;
        case 64:
            return write_memory_64;
        default:
            return nullptr;
        }
    }

    void int3() { db(0xCC); }
    void nop(size_t size = 1);

    void SetCodePtr(CodePtr ptr);
    void EnsurePatchLocationSize(CodePtr begin, size_t size);

#ifdef _WIN32
    Xbyak::Reg64 ABI_RETURN = rax;
    Xbyak::Reg64 ABI_PARAM1 = rcx;
    Xbyak::Reg64 ABI_PARAM2 = rdx;
    Xbyak::Reg64 ABI_PARAM3 = r8;
    Xbyak::Reg64 ABI_PARAM4 = r9;
#else
    Xbyak::Reg64 ABI_RETURN = rax;
    Xbyak::Reg64 ABI_PARAM1 = rdi;
    Xbyak::Reg64 ABI_PARAM2 = rsi;
    Xbyak::Reg64 ABI_PARAM3 = rdx;
    Xbyak::Reg64 ABI_PARAM4 = rcx;
#endif

private:
    UserCallbacks cb;

    struct Consts {
        Xbyak::Label FloatPositiveZero32;
        Xbyak::Label FloatNegativeZero32;
        Xbyak::Label FloatNaN32;
        Xbyak::Label FloatNonSignMask32;
        Xbyak::Label FloatPositiveZero64;
        Xbyak::Label FloatNegativeZero64;
        Xbyak::Label FloatNaN64;
        Xbyak::Label FloatNonSignMask64;
        Xbyak::Label FloatPenultimatePositiveDenormal64;
        Xbyak::Label FloatMinS32;
        Xbyak::Label FloatMaxS32;
        Xbyak::Label FloatMinU32;
        Xbyak::Label FloatMaxU32;
    } consts;
    void GenConstants();

    using RunCodeFuncType = void(*)(JitState*, CodePtr);
    RunCodeFuncType run_code = nullptr;
    void GenRunCode();

    const void* return_from_run_code = nullptr;
    const void* return_from_run_code_without_mxcsr_switch = nullptr;
    void GenReturnFromRunCode();

    const void* read_memory_8 = nullptr;
    const void* read_memory_16 = nullptr;
    const void* read_memory_32 = nullptr;
    const void* read_memory_64 = nullptr;
    const void* write_memory_8 = nullptr;
    const void* write_memory_16 = nullptr;
    const void* write_memory_32 = nullptr;
    const void* write_memory_64 = nullptr;
    void GenMemoryAccessors();
};

} // namespace BackendX64
} // namespace Dynarmic
