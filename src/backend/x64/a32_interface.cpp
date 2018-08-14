/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <memory>

#include <boost/icl/interval_set.hpp>
#include <fmt/format.h>

#include "backend/x64/a32_emit_x64.h"
#include "backend/x64/a32_jitstate.h"
#include "backend/x64/block_of_code.h"
#include "backend/x64/callback.h"
#include "backend/x64/devirtualize.h"
#include "backend/x64/jitstate_info.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "common/llvm_disassemble.h"
#include "common/scope_exit.h"
#include "dynarmic/A32/a32.h"
#include "dynarmic/A32/context.h"
#include "frontend/A32/translate/translate.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/location_descriptor.h"
#include "ir_opt/passes.h"

namespace Dynarmic::A32 {

using namespace BackendX64;

static RunCodeCallbacks GenRunCodeCallbacks(A32::UserCallbacks* cb, CodePtr (*LookupBlock)(void* lookup_block_arg), void* arg) {
    return RunCodeCallbacks{
        std::make_unique<ArgCallback>(LookupBlock, reinterpret_cast<u64>(arg)),
        std::make_unique<ArgCallback>(Devirtualize<&A32::UserCallbacks::AddTicks>(cb)),
        std::make_unique<ArgCallback>(Devirtualize<&A32::UserCallbacks::GetTicksRemaining>(cb)),
    };
}

struct Jit::Impl {
    Impl(Jit* jit, A32::UserConfig config)
            : block_of_code(GenRunCodeCallbacks(config.callbacks, &GetCurrentBlock, this), JitStateInfo{jit_state})
            , emitter(block_of_code, config, jit)
            , config(config)
            , jit_interface(jit)
    {}

    A32JitState jit_state;
    BlockOfCode block_of_code;
    A32EmitX64 emitter;

    const A32::UserConfig config;

    // Requests made during execution to invalidate the cache are queued up here.
    size_t invalid_cache_generation = 0;
    boost::icl::interval_set<u32> invalid_cache_ranges;
    bool invalidate_entire_cache = false;

    void Execute() {
        const u32 new_rsb_ptr = (jit_state.rsb_ptr - 1) & A32JitState::RSBPtrMask;
        if (jit_state.GetUniqueHash() == jit_state.rsb_location_descriptors[new_rsb_ptr]) {
            jit_state.rsb_ptr = new_rsb_ptr;
            block_of_code.RunCodeFrom(&jit_state, reinterpret_cast<CodePtr>(jit_state.rsb_codeptrs[new_rsb_ptr]));
        } else {
            block_of_code.RunCode(&jit_state);
        }
    }

    std::string Disassemble(const IR::LocationDescriptor& descriptor) {
        auto block = GetBasicBlock(descriptor);
        std::string result = fmt::format("address: {}\nsize: {} bytes\n", block.entrypoint, block.size);
        result += Common::DisassembleX64(block.entrypoint, reinterpret_cast<const char*>(block.entrypoint) + block.size);
        return result;
    }

    void PerformCacheInvalidation() {
        if (invalidate_entire_cache) {
            jit_state.ResetRSB();
            block_of_code.ClearCache();
            emitter.ClearCache();

            invalid_cache_ranges.clear();
            invalidate_entire_cache = false;
            invalid_cache_generation++;
            return;
        }

        if (invalid_cache_ranges.empty()) {
            return;
        }

        jit_state.ResetRSB();
        emitter.InvalidateCacheRanges(invalid_cache_ranges);
        invalid_cache_ranges.clear();
        invalid_cache_generation++;
    }

    void RequestCacheInvalidation() {
        if (jit_interface->is_executing) {
            jit_state.halt_requested = true;
            return;
        }

        PerformCacheInvalidation();
    }

private:
    Jit* jit_interface;

    static CodePtr GetCurrentBlock(void* this_voidptr) {
        Jit::Impl& this_ = *static_cast<Jit::Impl*>(this_voidptr);
        A32JitState& jit_state = this_.jit_state;

        u32 pc = jit_state.Reg[15];
        A32::PSR cpsr{jit_state.Cpsr()};
        A32::FPSCR fpscr{jit_state.FPSCR_mode};
        A32::LocationDescriptor descriptor{pc, cpsr, fpscr};

        return this_.GetBasicBlock(descriptor).entrypoint;
    }

    A32EmitX64::BlockDescriptor GetBasicBlock(IR::LocationDescriptor descriptor) {
        auto block = emitter.GetBasicBlock(descriptor);
        if (block)
            return *block;

        constexpr size_t MINIMUM_REMAINING_CODESIZE = 1 * 1024 * 1024;
        if (block_of_code.SpaceRemaining() < MINIMUM_REMAINING_CODESIZE) {
            invalidate_entire_cache = true;
            PerformCacheInvalidation();
        }

        IR::Block ir_block = A32::Translate(A32::LocationDescriptor{descriptor}, [this](u32 vaddr) { return config.callbacks->MemoryReadCode(vaddr); });
        Optimization::A32GetSetElimination(ir_block);
        Optimization::DeadCodeElimination(ir_block);
        Optimization::A32ConstantMemoryReads(ir_block, config.callbacks);
        Optimization::ConstantPropagation(ir_block);
        Optimization::DeadCodeElimination(ir_block);
        Optimization::VerificationPass(ir_block);
        return emitter.Emit(ir_block);
    }
};

Jit::Jit(UserConfig config) : impl(std::make_unique<Impl>(this, config)) {}

Jit::~Jit() {}

void Jit::Run() {
    ASSERT(!is_executing);
    is_executing = true;
    SCOPE_EXIT { this->is_executing = false; };

    impl->jit_state.halt_requested = false;

    impl->Execute();

    impl->PerformCacheInvalidation();
}

void Jit::ClearCache() {
    impl->invalidate_entire_cache = true;
    impl->RequestCacheInvalidation();
}

void Jit::InvalidateCacheRange(std::uint32_t start_address, std::size_t length) {
    impl->invalid_cache_ranges.add(boost::icl::discrete_interval<u32>::closed(start_address, static_cast<u32>(start_address + length - 1)));
    impl->RequestCacheInvalidation();
}

void Jit::Reset() {
    ASSERT(!is_executing);
    impl->jit_state = {};
}

void Jit::HaltExecution() {
    impl->jit_state.halt_requested = true;
}

std::array<u32, 16>& Jit::Regs() {
    return impl->jit_state.Reg;
}
const std::array<u32, 16>& Jit::Regs() const {
    return impl->jit_state.Reg;
}

std::array<u32, 64>& Jit::ExtRegs() {
    return impl->jit_state.ExtReg;
}

const std::array<u32, 64>& Jit::ExtRegs() const {
    return impl->jit_state.ExtReg;
}

u32 Jit::Cpsr() const {
    return impl->jit_state.Cpsr();
}

void Jit::SetCpsr(u32 value) {
    return impl->jit_state.SetCpsr(value);
}

u32 Jit::Fpscr() const {
    return impl->jit_state.Fpscr();
}

void Jit::SetFpscr(u32 value) {
    return impl->jit_state.SetFpscr(value);
}

Context Jit::SaveContext() const {
    Context ctx;
    SaveContext(ctx);
    return ctx;
}

struct Context::Impl {
    A32JitState jit_state;
    size_t invalid_cache_generation;
};

Context::Context() : impl(std::make_unique<Context::Impl>()) { impl->jit_state.ResetRSB(); }
Context::~Context() = default;
Context::Context(const Context& ctx) : impl(std::make_unique<Context::Impl>(*ctx.impl)) {}
Context::Context(Context&& ctx) : impl(std::move(ctx.impl)) {}
Context& Context::operator=(const Context& ctx) {
    *impl = *ctx.impl;
    return *this;
}
Context& Context::operator=(Context&& ctx) {
    impl = std::move(ctx.impl);
    return *this;
}

std::array<std::uint32_t, 16>& Context::Regs() {
    return impl->jit_state.Reg;
}
const std::array<std::uint32_t, 16>& Context::Regs() const {
    return impl->jit_state.Reg;
}
std::array<std::uint32_t, 64>& Context::ExtRegs() {
    return impl->jit_state.ExtReg;
}
const std::array<std::uint32_t, 64>& Context::ExtRegs() const {
    return impl->jit_state.ExtReg;
}

/// View and modify CPSR.
std::uint32_t Context::Cpsr() const {
    return impl->jit_state.Cpsr();
}
void Context::SetCpsr(std::uint32_t value) {
    impl->jit_state.SetCpsr(value);
}

/// View and modify FPSCR.
std::uint32_t Context::Fpscr() const {
    return impl->jit_state.Fpscr();
}
void Context::SetFpscr(std::uint32_t value) {
    return impl->jit_state.SetFpscr(value);
}

void TransferJitState(A32JitState& dest, const A32JitState& src, bool reset_rsb) {
    dest.CPSR_ge = src.CPSR_ge;
    dest.CPSR_et = src.CPSR_et;
    dest.CPSR_q = src.CPSR_q;
    dest.CPSR_nzcv = src.CPSR_nzcv;
    dest.CPSR_jaifm = src.CPSR_jaifm;
    dest.Reg = src.Reg;
    dest.ExtReg = src.ExtReg;
    dest.guest_MXCSR = src.guest_MXCSR;
    dest.FPSCR_IDC = src.FPSCR_IDC;
    dest.FPSCR_UFC = src.FPSCR_UFC;
    dest.FPSCR_mode = src.FPSCR_mode;
    dest.FPSCR_nzcv = src.FPSCR_nzcv;
    if (reset_rsb) {
        dest.ResetRSB();
    } else {
        dest.rsb_ptr = src.rsb_ptr;
        dest.rsb_location_descriptors = src.rsb_location_descriptors;
        dest.rsb_codeptrs = src.rsb_codeptrs;
    }
}

void Jit::SaveContext(Context& ctx) const {
    TransferJitState(ctx.impl->jit_state, impl->jit_state, false);
    ctx.impl->invalid_cache_generation = impl->invalid_cache_generation;
}

void Jit::LoadContext(const Context& ctx) {
    bool reset_rsb = ctx.impl->invalid_cache_generation != impl->invalid_cache_generation;
    TransferJitState(impl->jit_state, ctx.impl->jit_state, reset_rsb);
}

std::string Jit::Disassemble(const IR::LocationDescriptor& descriptor) {
    return impl->Disassemble(descriptor);
}

} // namespace Dynarmic::A32
