/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <memory>
#include <type_traits>

#include <xbyak.h>
#include <xbyak_util.h>

#include "backend/x64/callback.h"
#include "backend/x64/constant_pool.h"
#include "backend/x64/jitstate_info.h"
#include "common/common_types.h"

namespace Dynarmic::BackendX64 {

using CodePtr = const void*;

struct RunCodeCallbacks {
    std::unique_ptr<Callback> LookupBlock;
    std::unique_ptr<Callback> AddTicks;
    std::unique_ptr<Callback> GetTicksRemaining;
};

class BlockOfCode final : public Xbyak::CodeGenerator {
public:
    BlockOfCode(RunCodeCallbacks cb, JitStateInfo jsi);
    /// Call when external emitters have finished emitting their preludes.
    void PreludeComplete();

    /// Clears this block of code and resets code pointer to beginning.
    void ClearCache();
    /// Calculates how much space is remaining to use. This is the minimum of near code and far code.
    size_t SpaceRemaining() const;

    /// Runs emulated code.
    void RunCode(void* jit_state) const;
    /// Runs emulated code from code_ptr.
    void RunCodeFrom(void* jit_state, CodePtr code_ptr) const;
    /// Code emitter: Returns to dispatcher
    void ReturnFromRunCode(bool mxcsr_already_exited = false);
    /// Code emitter: Returns to dispatcher, forces return to host
    void ForceReturnFromRunCode(bool mxcsr_already_exited = false);
    /// Code emitter: Makes guest MXCSR the current MXCSR
    void SwitchMxcsrOnEntry();
    /// Code emitter: Makes saved host MXCSR the current MXCSR
    void SwitchMxcsrOnExit();
    /// Code emitter: Updates cycles remaining my calling cb.AddTicks and cb.GetTicksRemaining
    /// @note this clobbers ABI callee-save registers
    void UpdateTicks();

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

    Xbyak::Address MConst(const Xbyak::AddressFrame& frame, u64 lower, u64 upper = 0);

    /// Far code sits far away from the near code. Execution remains primarily in near code.
    /// "Cold" / Rarely executed instructions sit in far code, so the CPU doesn't fetch them unless necessary.
    void SwitchToFarCode();
    void SwitchToNearCode();

    CodePtr GetCodeBegin() const;

    const void* GetReturnFromRunCodeAddress() const {
        return return_from_run_code[0];
    }

    const void* GetForceReturnFromRunCodeAddress() const {
        return return_from_run_code[FORCE_RETURN];
    }

    void int3() { db(0xCC); }

    /// Allocate memory of `size` bytes from the same block of memory the code is in.
    /// This is useful for objects that need to be placed close to or within code.
    /// The lifetime of this memory is the same as the code around it.
    void* AllocateFromCodeSpace(size_t size);

    void SetCodePtr(CodePtr code_ptr);
    void EnsurePatchLocationSize(CodePtr begin, size_t size);

    // ABI registers
#ifdef _WIN32
    static const Xbyak::Reg64 ABI_RETURN;
    static const Xbyak::Reg64 ABI_PARAM1;
    static const Xbyak::Reg64 ABI_PARAM2;
    static const Xbyak::Reg64 ABI_PARAM3;
    static const Xbyak::Reg64 ABI_PARAM4;
    static const std::array<Xbyak::Reg64, 4> ABI_PARAMS;
#else
    static const Xbyak::Reg64 ABI_RETURN;
    static const Xbyak::Reg64 ABI_RETURN2;
    static const Xbyak::Reg64 ABI_PARAM1;
    static const Xbyak::Reg64 ABI_PARAM2;
    static const Xbyak::Reg64 ABI_PARAM3;
    static const Xbyak::Reg64 ABI_PARAM4;
    static const Xbyak::Reg64 ABI_PARAM5;
    static const Xbyak::Reg64 ABI_PARAM6;
    static const std::array<Xbyak::Reg64, 6> ABI_PARAMS;
#endif

    bool DoesCpuSupport(Xbyak::util::Cpu::Type type) const;

    JitStateInfo GetJitStateInfo() const { return jsi; }

private:
    RunCodeCallbacks cb;
    JitStateInfo jsi;

    bool prelude_complete = false;
    CodePtr near_code_begin;
    CodePtr far_code_begin;

    ConstantPool constant_pool;

    bool in_far_code = false;
    CodePtr near_code_ptr;
    CodePtr far_code_ptr;

    using RunCodeFuncType = void(*)(void*);
    using RunCodeFromFuncType = void(*)(void*, CodePtr);
    RunCodeFuncType run_code = nullptr;
    RunCodeFromFuncType run_code_from = nullptr;
    static constexpr size_t MXCSR_ALREADY_EXITED = 1 << 0;
    static constexpr size_t FORCE_RETURN = 1 << 1;
    std::array<const void*, 4> return_from_run_code;
    void GenRunCode();

    class ExceptionHandler final {
    public:
        ExceptionHandler();
        ~ExceptionHandler();

        void Register(BlockOfCode& code);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
    ExceptionHandler exception_handler;

    Xbyak::util::Cpu cpu_info;
};

} // namespace Dynarmic::BackendX64
