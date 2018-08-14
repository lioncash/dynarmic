/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include <cstring>
#include <limits>

#include <xbyak.h>

#include "backend/x64/a32_jitstate.h"
#include "backend/x64/abi.h"
#include "backend/x64/block_of_code.h"
#include "common/assert.h"

namespace Dynarmic::BackendX64 {

#ifdef _WIN32
const Xbyak::Reg64 BlockOfCode::ABI_RETURN = Xbyak::util::rax;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM1 = Xbyak::util::rcx;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM2 = Xbyak::util::rdx;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM3 = Xbyak::util::r8;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM4 = Xbyak::util::r9;
const std::array<Xbyak::Reg64, 4> BlockOfCode::ABI_PARAMS = {BlockOfCode::ABI_PARAM1, BlockOfCode::ABI_PARAM2, BlockOfCode::ABI_PARAM3, BlockOfCode::ABI_PARAM4};
#else
const Xbyak::Reg64 BlockOfCode::ABI_RETURN = Xbyak::util::rax;
const Xbyak::Reg64 BlockOfCode::ABI_RETURN2 = Xbyak::util::rdx;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM1 = Xbyak::util::rdi;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM2 = Xbyak::util::rsi;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM3 = Xbyak::util::rdx;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM4 = Xbyak::util::rcx;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM5 = Xbyak::util::r8;
const Xbyak::Reg64 BlockOfCode::ABI_PARAM6 = Xbyak::util::r9;
const std::array<Xbyak::Reg64, 6> BlockOfCode::ABI_PARAMS = {BlockOfCode::ABI_PARAM1, BlockOfCode::ABI_PARAM2, BlockOfCode::ABI_PARAM3, BlockOfCode::ABI_PARAM4, BlockOfCode::ABI_PARAM5, BlockOfCode::ABI_PARAM6};
#endif

constexpr size_t TOTAL_CODE_SIZE = 128 * 1024 * 1024;
constexpr size_t FAR_CODE_OFFSET = 100 * 1024 * 1024;
constexpr size_t CONSTANT_POOL_SIZE = 2 * 1024 * 1024;

BlockOfCode::BlockOfCode(RunCodeCallbacks cb, JitStateInfo jsi)
        : Xbyak::CodeGenerator(TOTAL_CODE_SIZE)
        , cb(std::move(cb))
        , jsi(jsi)
        , constant_pool(*this, CONSTANT_POOL_SIZE)
{
    GenRunCode();
    exception_handler.Register(*this);
}

void BlockOfCode::PreludeComplete() {
    prelude_complete = true;
    near_code_begin = getCurr();
    far_code_begin = getCurr() + FAR_CODE_OFFSET;
    ClearCache();
}

void BlockOfCode::ClearCache() {
    ASSERT(prelude_complete);
    in_far_code = false;
    near_code_ptr = near_code_begin;
    far_code_ptr = far_code_begin;
    SetCodePtr(near_code_begin);
}

size_t BlockOfCode::SpaceRemaining() const {
    ASSERT(prelude_complete);
    // This function provides an underestimate of near-code-size but that's okay.
    // (Why? The maximum size of near code should be measured from near_code_begin, not top_.)
    // These are offsets from Xbyak::CodeArray::top_.
    std::size_t far_code_offset, near_code_offset;
    if (in_far_code) {
        near_code_offset = static_cast<const u8*>(near_code_ptr) - getCode();
        far_code_offset = getCurr() - getCode();
    } else {
        near_code_offset = getCurr() - getCode();
        far_code_offset = static_cast<const u8*>(far_code_ptr) - getCode();
    }
    if (far_code_offset > TOTAL_CODE_SIZE)
        return 0;
    if (near_code_offset > FAR_CODE_OFFSET)
        return 0;
    return std::min(TOTAL_CODE_SIZE - far_code_offset, FAR_CODE_OFFSET - near_code_offset);
}

void BlockOfCode::RunCode(void* jit_state) const {
    run_code(jit_state);
}

void BlockOfCode::RunCodeFrom(void* jit_state, CodePtr code_ptr) const {
    run_code_from(jit_state, code_ptr);
}

void BlockOfCode::ReturnFromRunCode(bool mxcsr_already_exited) {
    size_t index = 0;
    if (mxcsr_already_exited)
        index |= MXCSR_ALREADY_EXITED;
    jmp(return_from_run_code[index]);
}

void BlockOfCode::ForceReturnFromRunCode(bool mxcsr_already_exited) {
    size_t index = FORCE_RETURN;
    if (mxcsr_already_exited)
        index |= MXCSR_ALREADY_EXITED;
    jmp(return_from_run_code[index]);
}

void BlockOfCode::GenRunCode() {
    Xbyak::Label loop, enter_mxcsr_then_loop;

    align();
    run_code_from = getCurr<RunCodeFromFuncType>();

    ABI_PushCalleeSaveRegistersAndAdjustStack(*this);

    mov(r15, ABI_PARAM1);
    mov(r14, ABI_PARAM2); // save temporarily in non-volatile register

    cb.GetTicksRemaining->EmitCall(*this);
    mov(qword[r15 + jsi.offsetof_cycles_to_run], ABI_RETURN);
    mov(qword[r15 + jsi.offsetof_cycles_remaining], ABI_RETURN);

    SwitchMxcsrOnEntry();
    jmp(r14);

    align();
    run_code = getCurr<RunCodeFuncType>();

    // This serves two purposes:
    // 1. It saves all the registers we as a callee need to save.
    // 2. It aligns the stack so that the code the JIT emits can assume
    //    that the stack is appropriately aligned for CALLs.
    ABI_PushCalleeSaveRegistersAndAdjustStack(*this);

    mov(r15, ABI_PARAM1);

    cb.GetTicksRemaining->EmitCall(*this);
    mov(qword[r15 + jsi.offsetof_cycles_to_run], ABI_RETURN);
    mov(qword[r15 + jsi.offsetof_cycles_remaining], ABI_RETURN);

    L(enter_mxcsr_then_loop);
    SwitchMxcsrOnEntry();
    L(loop);
    cb.LookupBlock->EmitCall(*this);

    jmp(ABI_RETURN);

    // Return from run code variants
    const auto emit_return_from_run_code = [this, &loop, &enter_mxcsr_then_loop](bool mxcsr_already_exited, bool force_return){
        if (!force_return) {
            cmp(qword[r15 + jsi.offsetof_cycles_remaining], 0);
            jg(mxcsr_already_exited ? enter_mxcsr_then_loop : loop);
        }

        if (!mxcsr_already_exited) {
            SwitchMxcsrOnExit();
        }

        cb.AddTicks->EmitCall(*this, [this](RegList param) {
            mov(param[0], qword[r15 + jsi.offsetof_cycles_to_run]);
            sub(param[0], qword[r15 + jsi.offsetof_cycles_remaining]);
        });

        ABI_PopCalleeSaveRegistersAndAdjustStack(*this);
        ret();
    };

    align();
    return_from_run_code[0] = getCurr<const void*>();
    emit_return_from_run_code(false, false);

    align();
    return_from_run_code[MXCSR_ALREADY_EXITED] = getCurr<const void*>();
    emit_return_from_run_code(true, false);

    align();
    return_from_run_code[FORCE_RETURN] = getCurr<const void*>();
    emit_return_from_run_code(false, true);

    align();
    return_from_run_code[MXCSR_ALREADY_EXITED | FORCE_RETURN] = getCurr<const void*>();
    emit_return_from_run_code(true, true);
}

void BlockOfCode::SwitchMxcsrOnEntry() {
    stmxcsr(dword[r15 + jsi.offsetof_save_host_MXCSR]);
    ldmxcsr(dword[r15 + jsi.offsetof_guest_MXCSR]);
}

void BlockOfCode::SwitchMxcsrOnExit() {
    stmxcsr(dword[r15 + jsi.offsetof_guest_MXCSR]);
    ldmxcsr(dword[r15 + jsi.offsetof_save_host_MXCSR]);
}

void BlockOfCode::UpdateTicks() {
    cb.AddTicks->EmitCall(*this, [this](RegList param) {
        mov(param[0], qword[r15 + jsi.offsetof_cycles_to_run]);
        sub(param[0], qword[r15 + jsi.offsetof_cycles_remaining]);
    });

    cb.GetTicksRemaining->EmitCall(*this);
    mov(qword[r15 + jsi.offsetof_cycles_to_run], ABI_RETURN);
    mov(qword[r15 + jsi.offsetof_cycles_remaining], ABI_RETURN);
}

Xbyak::Address BlockOfCode::MConst(const Xbyak::AddressFrame& frame, u64 lower, u64 upper) {
    return constant_pool.GetConstant(frame, lower, upper);
}

void BlockOfCode::SwitchToFarCode() {
    ASSERT(prelude_complete);
    ASSERT(!in_far_code);
    in_far_code = true;
    near_code_ptr = getCurr();
    SetCodePtr(far_code_ptr);

    ASSERT_MSG(near_code_ptr < far_code_begin, "Near code has overwritten far code!");
}

void BlockOfCode::SwitchToNearCode() {
    ASSERT(prelude_complete);
    ASSERT(in_far_code);
    in_far_code = false;
    far_code_ptr = getCurr();
    SetCodePtr(near_code_ptr);
}

CodePtr BlockOfCode::GetCodeBegin() const {
    return near_code_begin;
}

void* BlockOfCode::AllocateFromCodeSpace(size_t alloc_size) {
    if (size_ + alloc_size >= maxSize_) {
        throw Xbyak::Error(Xbyak::ERR_CODE_IS_TOO_BIG);
    }

    void* ret = getCurr<void*>();
    size_ += alloc_size;
    memset(ret, 0, alloc_size);
    return ret;
}

void BlockOfCode::SetCodePtr(CodePtr code_ptr) {
    // The "size" defines where top_, the insertion point, is.
    size_t required_size = reinterpret_cast<const u8*>(code_ptr) - getCode();
    setSize(required_size);
}

void BlockOfCode::EnsurePatchLocationSize(CodePtr begin, size_t size) {
    size_t current_size = getCurr<const u8*>() - reinterpret_cast<const u8*>(begin);
    ASSERT(current_size <= size);
    nop(size - current_size);
}

bool BlockOfCode::DoesCpuSupport(Xbyak::util::Cpu::Type type) const {
#ifdef DYNARMIC_ENABLE_CPU_FEATURE_DETECTION
    return cpu_info.has(type);
#else
    (void)type;
    return false;
#endif
}

} // namespace Dynarmic::BackendX64
