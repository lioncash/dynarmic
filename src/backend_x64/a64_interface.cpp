/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>
#include <memory>

#include <boost/icl/interval_set.hpp>

#include "backend_x64/a64_emit_x64.h"
#include "backend_x64/a64_jitstate.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/devirtualize.h"
#include "backend_x64/jitstate_info.h"
#include "common/assert.h"
#include "common/scope_exit.h"
#include "dynarmic/A64/a64.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"
#include "ir_opt/passes.h"

namespace Dynarmic {
namespace A64 {

using namespace BackendX64;

static RunCodeCallbacks GenRunCodeCallbacks(A64::UserCallbacks* cb, CodePtr (*LookupBlock)(void* lookup_block_arg), void* arg) {
    return RunCodeCallbacks{
        std::make_unique<ArgCallback>(LookupBlock, reinterpret_cast<u64>(arg)),
        std::make_unique<ArgCallback>(DEVIRT(cb, &A64::UserCallbacks::AddTicks)),
        std::make_unique<ArgCallback>(DEVIRT(cb, &A64::UserCallbacks::GetTicksRemaining)),
    };
}

struct Jit::Impl final {
public:
    explicit Impl(UserConfig conf)
        : conf(conf) 
        , block_of_code(GenRunCodeCallbacks(conf.callbacks, &GetCurrentBlockThunk, this), JitStateInfo{jit_state})
        , emitter(&block_of_code, conf)
    {}

    ~Impl() = default;

    void Run() {
        ASSERT(!is_executing);
        is_executing = true;
        SCOPE_EXIT({ this->is_executing = false; });
        jit_state.halt_requested = false;

        // TODO: Check code alignment
        block_of_code.RunCode(&jit_state);

        PerformRequestedCacheInvalidation();
    }

    void ClearCache() {
        invalidate_entire_cache = true;
        RequestCacheInvalidation();
    }

    void InvalidateCacheRange(u64 start_address, size_t length) {
        const auto end_address = static_cast<u64>(start_address + length - 1);
        const auto range = boost::icl::discrete_interval<u64>::closed(start_address, end_address);
        invalid_cache_ranges.add(range);
        RequestCacheInvalidation();
    }

    void Reset() {
        ASSERT(!is_executing);
        jit_state = {};
    }

    void HaltExecution() {
        jit_state.halt_requested = true;
    }

    u64 GetSP() const {
        return jit_state.sp;
    }

    void SetSP(u64 value) {
        jit_state.sp = value;
    }

    u64 GetPC() const {
        return jit_state.pc;
    }

    void SetPC(u64 value) {
        jit_state.pc = value;
    }

    u64 GetRegister(size_t index) const {
        if (index == 31)
            return GetSP();
        return jit_state.reg.at(index);
    }

    void SetRegister(size_t index, u64 value) {
        if (index == 31)
            return SetSP(value);
        jit_state.reg.at(index) = value;
    }

    std::array<u64, 31> GetRegisters() const {
        return jit_state.reg;
    }

    void SetRegisters(const std::array<u64, 31>& value) {
        jit_state.reg = value;
    }

    Vector GetVector(size_t index) const {
        return {jit_state.vec.at(index * 2), jit_state.vec.at(index * 2 + 1)};
    }

    void SetVector(size_t index, Vector value) {
        jit_state.vec.at(index * 2) = value[0];
        jit_state.vec.at(index * 2 + 1) = value[1];
    }

    std::array<Jit::Vector, 32> GetVectors() const {
        std::array<Jit::Vector, 32> ret;
        static_assert(sizeof(ret) == sizeof(jit_state.vec));
        std::memcpy(ret.data(), jit_state.vec.data(), sizeof(jit_state.vec));
        return ret;
    }

    void SetVectors(const std::array<Jit::Vector, 32>& value) {
        static_assert(sizeof(value) == sizeof(jit_state.vec));
        std::memcpy(jit_state.vec.data(), value.data(), sizeof(jit_state.vec));
    }

    u32 GetFpcr() const {
        return jit_state.GetFpcr();
    }

    void SetFpcr(u32 value) {
        jit_state.SetFpcr(value);
    }

    u32 GetPstate() const {
        return jit_state.GetPstate();
    }

    void SetPstate(u32 value) {
        jit_state.SetPstate(value);
    }

    bool IsExecuting() const {
        return is_executing;
    }

private:
    static CodePtr GetCurrentBlockThunk(void* thisptr) {
        Jit::Impl* this_ = reinterpret_cast<Jit::Impl*>(thisptr);
        return this_->GetCurrentBlock();
    }

    CodePtr GetCurrentBlock() {
        IR::LocationDescriptor current_location{jit_state.GetUniqueHash()};

        if (auto block = emitter.GetBasicBlock(current_location))
            return block->entrypoint;

        constexpr size_t MINIMUM_REMAINING_CODESIZE = 1 * 1024 * 1024;
        if (block_of_code.SpaceRemaining() < MINIMUM_REMAINING_CODESIZE) {
            // Immediately evacuate cache
            invalidate_entire_cache = true;
            PerformRequestedCacheInvalidation();
        }

        // JIT Compile
        IR::Block ir_block = A64::Translate(A64::LocationDescriptor{current_location}, [this](u64 vaddr) { return conf.callbacks->MemoryReadCode(vaddr); });
        Optimization::DeadCodeElimination(ir_block);
        // printf("%s\n", IR::DumpBlock(ir_block).c_str());
        Optimization::VerificationPass(ir_block);
        return emitter.Emit(ir_block).entrypoint;
    }

    void RequestCacheInvalidation() {
        if (is_executing) {
            jit_state.halt_requested = true;
            return;
        }

        PerformRequestedCacheInvalidation();
    }

    void PerformRequestedCacheInvalidation() {
        if (!invalidate_entire_cache && invalid_cache_ranges.empty()) {
            return;
        }

        jit_state.ResetRSB();
        if (invalidate_entire_cache) {
            block_of_code.ClearCache();
            emitter.ClearCache();
        } else {
            emitter.InvalidateCacheRanges(invalid_cache_ranges);
        }
        invalid_cache_ranges.clear();
        invalidate_entire_cache = false;
    }

    bool is_executing = false;

    UserConfig conf;
    A64JitState jit_state;
    BlockOfCode block_of_code;
    A64EmitX64 emitter;

    bool invalidate_entire_cache = false;
    boost::icl::interval_set<u64> invalid_cache_ranges;
};

Jit::Jit(UserConfig conf)
    : impl(std::make_unique<Jit::Impl>(conf)) {}

Jit::~Jit() = default;

void Jit::Run() {
    impl->Run();
}

void Jit::ClearCache() {
    impl->ClearCache();
}

void Jit::InvalidateCacheRange(u64 start_address, size_t length) {
    impl->InvalidateCacheRange(start_address, length);
}

void Jit::Reset() {
    impl->Reset();
}

void Jit::HaltExecution() {
    impl->HaltExecution();
}

u64 Jit::GetSP() const {
    return impl->GetSP();
}

void Jit::SetSP(u64 value) {
    impl->SetSP(value);
}

u64 Jit::GetPC() const {
    return impl->GetPC();
}

void Jit::SetPC(u64 value) {
    impl->SetPC(value);
}

u64 Jit::GetRegister(size_t index) const {
    return impl->GetRegister(index);
}

void Jit::SetRegister(size_t index, u64 value) {
    impl->SetRegister(index, value);
}

std::array<u64, 31> Jit::GetRegisters() const {
    return impl->GetRegisters();
}

void Jit::SetRegisters(const std::array<u64, 31>& value) {
    impl->SetRegisters(value);
}

Jit::Vector Jit::GetVector(size_t index) const {
    return impl->GetVector(index);
}

void Jit::SetVector(size_t index, Vector value) {
    impl->SetVector(index, value);
}

std::array<Jit::Vector, 32> Jit::GetVectors() const {
    return impl->GetVectors();
}

void Jit::SetVectors(const std::array<Jit::Vector, 32>& value) {
    impl->SetVectors(value);
}

u32 Jit::GetFpcr() const {
    return impl->GetFpcr();
}

void Jit::SetFpcr(u32 value) {
    impl->SetFpcr(value);
}

u32 Jit::GetPstate() const {
    return impl->GetPstate();
}

void Jit::SetPstate(u32 value) {
    impl->SetPstate(value);
}

bool Jit::IsExecuting() const {
    return impl->IsExecuting();
}

} // namespace A64
} // namespace Dynarmic
