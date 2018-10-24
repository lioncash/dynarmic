/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <initializer_list>

#include <dynarmic/A64/exclusive_monitor.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include "backend/x64/a64_emit_x64.h"
#include "backend/x64/a64_jitstate.h"
#include "backend/x64/abi.h"
#include "backend/x64/block_of_code.h"
#include "backend/x64/devirtualize.h"
#include "backend/x64/emit_x64.h"
#include "backend/x64/perf_map.h"
#include "common/address_range.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/scope_exit.h"
#include "common/variant_util.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

// TODO: Have ARM flags in host flags and not have them use up GPR registers unless necessary.
// TODO: Actually implement that proper instruction selector you've always wanted to sweetheart.

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;

A64EmitContext::A64EmitContext(const A64::UserConfig& conf, RegAlloc& reg_alloc, IR::Block& block)
    : EmitContext(reg_alloc, block), conf(conf) {}

A64::LocationDescriptor A64EmitContext::Location() const {
    return A64::LocationDescriptor{block.Location()};
}

FP::RoundingMode A64EmitContext::FPSCR_RMode() const {
    return Location().FPCR().RMode();
}

u32 A64EmitContext::FPCR() const {
    return Location().FPCR().Value();
}

bool A64EmitContext::FPSCR_FTZ() const {
    return Location().FPCR().FZ();
}

bool A64EmitContext::FPSCR_DN() const {
    return Location().FPCR().DN() || conf.floating_point_nan_accuracy == A64::UserConfig::NaNAccuracy::AlwaysForceDefaultNaN;
}

bool A64EmitContext::AccurateNaN() const {
    return conf.floating_point_nan_accuracy == A64::UserConfig::NaNAccuracy::Accurate;
}

A64EmitX64::A64EmitX64(BlockOfCode& code, A64::UserConfig conf, A64::Jit* jit_interface)
        : EmitX64(code), conf(conf), jit_interface{jit_interface} {
    GenMemory128Accessors();
    GenFastmemFallbacks();
    GenTerminalHandlers();
    code.PreludeComplete();
    ClearFastDispatchTable();
}

A64EmitX64::~A64EmitX64() = default;

A64EmitX64::BlockDescriptor A64EmitX64::Emit(IR::Block& block) {
    code.EnableWriting();
    SCOPE_EXIT { code.DisableWriting(); };

    code.align();
    const u8* const entrypoint = code.getCurr();

    // Start emitting.
    EmitCondPrelude(block);

    RegAlloc reg_alloc{code, A64JitState::SpillCount, SpillToOpArg<A64JitState>};
    A64EmitContext ctx{conf, reg_alloc, block};

    for (auto iter = block.begin(); iter != block.end(); ++iter) {
        IR::Inst* inst = &*iter;

        // Call the relevant Emit* member function.
        switch (inst->GetOpcode()) {

#define OPCODE(name, type, ...)                 \
        case IR::Opcode::name:                  \
            A64EmitX64::Emit##name(ctx, inst);  \
            break;
#define A32OPC(...)
#define A64OPC(name, type, ...)                    \
        case IR::Opcode::A64##name:                \
            A64EmitX64::EmitA64##name(ctx, inst);  \
            break;
#include "frontend/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC

        default:
            ASSERT_MSG(false, "Invalid opcode: {}", inst->GetOpcode());
            break;
        }

        ctx.reg_alloc.EndOfAllocScope();
    }

    reg_alloc.AssertNoMoreUses();

    EmitAddCycles(block.CycleCount());
    EmitX64::EmitTerminal(block.GetTerminal(), block.Location());
    code.int3();

    const size_t size = static_cast<size_t>(code.getCurr() - entrypoint);

    const A64::LocationDescriptor descriptor{block.Location()};
    const A64::LocationDescriptor end_location{block.EndLocation()};

    const auto range = boost::icl::discrete_interval<u64>::closed(descriptor.PC(), end_location.PC() - 1);
    block_ranges.AddRange(range, descriptor);

    return RegisterBlock(descriptor, entrypoint, size);
}

void A64EmitX64::ClearCache() {
    EmitX64::ClearCache();
    block_ranges.ClearCache();
    ClearFastDispatchTable();
}

void A64EmitX64::InvalidateCacheRanges(const boost::icl::interval_set<u64>& ranges) {
    InvalidateBasicBlocks(block_ranges.InvalidateRanges(ranges));
    ClearFastDispatchTable();
}

void A64EmitX64::ClearFastDispatchTable() {
    if (conf.enable_fast_dispatch) {
        fast_dispatch_table.fill({0xFFFFFFFFFFFFFFFFull, nullptr});
    }
}

void A64EmitX64::GenMemory128Accessors() {
    code.align();
    memory_read_128 = code.getCurr<void(*)()>();
#ifdef _WIN32
    Devirtualize<&A64::UserCallbacks::MemoryRead128>(conf.callbacks).EmitCallWithReturnPointer(code,
        [&](Xbyak::Reg64 return_value_ptr, RegList args) {
            code.mov(code.ABI_PARAM3, code.ABI_PARAM2);
            code.sub(rsp, 8 + 16 + ABI_SHADOW_SPACE);
            code.lea(return_value_ptr, ptr[rsp + ABI_SHADOW_SPACE]);
        });
    code.movups(xmm1, xword[code.ABI_RETURN]);
    code.add(rsp, 8 + 16 + ABI_SHADOW_SPACE);
#else
    code.sub(rsp, 8);
    Devirtualize<&A64::UserCallbacks::MemoryRead128>(conf.callbacks).EmitCall(code);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.movq(xmm1, code.ABI_RETURN);
        code.pinsrq(xmm1, code.ABI_RETURN2, 1);
    } else {
        code.movq(xmm1, code.ABI_RETURN);
        code.movq(xmm2, code.ABI_RETURN2);
        code.punpcklqdq(xmm1, xmm2);
    }
    code.add(rsp, 8);
#endif
    code.ret();
    PerfMapRegister(memory_read_128, code.getCurr(), "a64_memory_read_128");

    code.align();
    memory_write_128 = code.getCurr<void(*)()>();
#ifdef _WIN32
    code.sub(rsp, 8 + 16 + ABI_SHADOW_SPACE);
    code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE]);
    code.movaps(xword[code.ABI_PARAM3], xmm1);
    Devirtualize<&A64::UserCallbacks::MemoryWrite128>(conf.callbacks).EmitCall(code);
    code.add(rsp, 8 + 16 + ABI_SHADOW_SPACE);
#else
    code.sub(rsp, 8);
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code.movq(code.ABI_PARAM3, xmm1);
        code.pextrq(code.ABI_PARAM4, xmm1, 1);
    } else {
        code.movq(code.ABI_PARAM3, xmm1);
        code.punpckhqdq(xmm1, xmm1);
        code.movq(code.ABI_PARAM4, xmm1);
    }
    Devirtualize<&A64::UserCallbacks::MemoryWrite128>(conf.callbacks).EmitCall(code);
    code.add(rsp, 8);
#endif
    code.ret();
    PerfMapRegister(memory_read_128, code.getCurr(), "a64_memory_write_128");
}

void A64EmitX64::GenFastmemFallbacks() {
    const std::initializer_list<int> idxes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const std::vector<std::tuple<size_t, ArgCallback>> read_callbacks {
        {8, Devirtualize<&A64::UserCallbacks::MemoryRead8>(conf.callbacks)},
        {16, Devirtualize<&A64::UserCallbacks::MemoryRead16>(conf.callbacks)},
        {32, Devirtualize<&A64::UserCallbacks::MemoryRead32>(conf.callbacks)},
        {64, Devirtualize<&A64::UserCallbacks::MemoryRead64>(conf.callbacks)},
    };
    const std::vector<std::tuple<size_t, ArgCallback>> write_callbacks {
        {8, Devirtualize<&A64::UserCallbacks::MemoryWrite8>(conf.callbacks)},
        {16, Devirtualize<&A64::UserCallbacks::MemoryWrite16>(conf.callbacks)},
        {32, Devirtualize<&A64::UserCallbacks::MemoryWrite32>(conf.callbacks)},
        {64, Devirtualize<&A64::UserCallbacks::MemoryWrite64>(conf.callbacks)},
    };

    for (int vaddr_idx : idxes) {
        if (vaddr_idx == 4 || vaddr_idx == 15) {
            continue;
        }

        for (int value_idx : idxes) {
            code.align();
            read_fallbacks[std::make_tuple(128, vaddr_idx, value_idx)] = code.getCurr<void(*)()>();
            ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(value_idx));
            if (vaddr_idx != code.ABI_PARAM2.getIdx()) {
                code.mov(code.ABI_PARAM2, Xbyak::Reg64{vaddr_idx});
            }
            code.call(memory_read_128);
            if (value_idx != 1) {
                code.movaps(Xbyak::Xmm{value_idx}, xmm1);
            }
            ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocXmmIdx(value_idx));
            code.ret();
            PerfMapRegister(read_fallbacks[std::make_tuple(128, vaddr_idx, value_idx)], code.getCurr(), "a64_read_fallback_128");

            code.align();
            write_fallbacks[std::make_tuple(128, vaddr_idx, value_idx)] = code.getCurr<void(*)()>();
            ABI_PushCallerSaveRegistersAndAdjustStack(code);
            if (vaddr_idx != code.ABI_PARAM2.getIdx()) {
                code.mov(code.ABI_PARAM2, Xbyak::Reg64{vaddr_idx});
            }
            if (value_idx != 1) {
                code.movaps(xmm1, Xbyak::Xmm{value_idx});
            }
            code.call(memory_write_128);
            ABI_PopCallerSaveRegistersAndAdjustStack(code);
            code.ret();
            PerfMapRegister(write_fallbacks[std::make_tuple(128, vaddr_idx, value_idx)], code.getCurr(), "a64_write_fallback_128");

            if (value_idx == 4 || value_idx == 15) {
                continue;
            }

            for (auto& [bitsize, callback] : read_callbacks) {
                code.align();
                read_fallbacks[std::make_tuple(bitsize, vaddr_idx, value_idx)] = code.getCurr<void(*)()>();
                ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, HostLocRegIdx(value_idx));
                if (vaddr_idx != code.ABI_PARAM2.getIdx()) {
                    code.mov(code.ABI_PARAM2, Xbyak::Reg64{vaddr_idx});
                }
                callback.EmitCall(code);
                if (value_idx != code.ABI_RETURN.getIdx()) {
                    code.mov(Xbyak::Reg64{value_idx}, code.ABI_RETURN);
                }
                ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, HostLocRegIdx(value_idx));
                code.ret();
                PerfMapRegister(read_fallbacks[std::make_tuple(bitsize, vaddr_idx, value_idx)], code.getCurr(), fmt::format("a64_read_fallback_{}", bitsize));
            }

            for (auto& [bitsize, callback] : write_callbacks) {
                code.align();
                write_fallbacks[std::make_tuple(bitsize, vaddr_idx, value_idx)] = code.getCurr<void(*)()>();
                ABI_PushCallerSaveRegistersAndAdjustStack(code);
                if (vaddr_idx == code.ABI_PARAM3.getIdx() && value_idx == code.ABI_PARAM2.getIdx()) {
                    code.xchg(code.ABI_PARAM2, code.ABI_PARAM3);
                } else if (vaddr_idx == code.ABI_PARAM3.getIdx()) {
                    code.mov(code.ABI_PARAM2, Xbyak::Reg64{vaddr_idx});
                    if (value_idx != code.ABI_PARAM3.getIdx()) {
                        code.mov(code.ABI_PARAM3, Xbyak::Reg64{value_idx});
                    }
                } else {
                    if (value_idx != code.ABI_PARAM3.getIdx()) {
                        code.mov(code.ABI_PARAM3, Xbyak::Reg64{value_idx});
                    }
                    if (vaddr_idx != code.ABI_PARAM2.getIdx()) {
                        code.mov(code.ABI_PARAM2, Xbyak::Reg64{vaddr_idx});
                    }
                }
                callback.EmitCall(code);
                ABI_PopCallerSaveRegistersAndAdjustStack(code);
                code.ret();
                PerfMapRegister(write_fallbacks[std::make_tuple(bitsize, vaddr_idx, value_idx)], code.getCurr(), fmt::format("a64_write_fallback_{}", bitsize));
            }
        }
    }
}

void A64EmitX64::GenTerminalHandlers() {
    // PC ends up in rbp, location_descriptor ends up in rbx
    const auto calculate_location_descriptor = [this] {
        // This calculation has to match up with A64::LocationDescriptor::UniqueHash
        // TODO: Optimization is available here based on known state of FPSCR_mode and CPSR_et.
        code.mov(rbp, qword[r15 + offsetof(A64JitState, pc)]);
        code.mov(rcx, A64::LocationDescriptor::PC_MASK);
        code.and_(rcx, rbp);
        code.mov(ebx, dword[r15 + offsetof(A64JitState, fpcr)]);
        code.and_(ebx, A64::LocationDescriptor::FPCR_MASK);
        code.shl(ebx, 37);
        code.or_(rbx, rcx);
    };

    Xbyak::Label fast_dispatch_cache_miss, rsb_cache_miss;

    code.align();
    terminal_handler_pop_rsb_hint = code.getCurr<const void*>();
    calculate_location_descriptor();
    code.mov(eax, dword[r15 + offsetof(A64JitState, rsb_ptr)]);
    code.sub(eax, 1);
    code.and_(eax, u32(A64JitState::RSBPtrMask));
    code.mov(dword[r15 + offsetof(A64JitState, rsb_ptr)], eax);
    code.cmp(rbx, qword[r15 + offsetof(A64JitState, rsb_location_descriptors) + rax * sizeof(u64)]);
    if (conf.enable_fast_dispatch) {
        code.jne(rsb_cache_miss);
    } else {
        code.jne(code.GetReturnFromRunCodeAddress());
    }
    code.mov(rax, qword[r15 + offsetof(A64JitState, rsb_codeptrs) + rax * sizeof(u64)]);
    code.jmp(rax);
    PerfMapRegister(terminal_handler_pop_rsb_hint, code.getCurr(), "a64_terminal_handler_pop_rsb_hint");

    if (conf.enable_fast_dispatch) {
        code.align();
        terminal_handler_fast_dispatch_hint = code.getCurr<const void*>();
        calculate_location_descriptor();
        code.L(rsb_cache_miss);
        code.mov(r12, reinterpret_cast<u64>(fast_dispatch_table.data()));
        if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE42)) {
            code.crc32(rbp, r12d);
        }
        code.and_(ebp, fast_dispatch_table_mask);
        code.lea(rbp, ptr[r12 + rbp]);
        code.cmp(rbx, qword[rbp + offsetof(FastDispatchEntry, location_descriptor)]);
        code.jne(fast_dispatch_cache_miss);
        code.jmp(ptr[rbp + offsetof(FastDispatchEntry, code_ptr)]);
        code.L(fast_dispatch_cache_miss);
        code.mov(qword[rbp + offsetof(FastDispatchEntry, location_descriptor)], rbx);
        code.LookupBlock();
        code.mov(ptr[rbp + offsetof(FastDispatchEntry, code_ptr)], rax);
        code.jmp(rax);
        PerfMapRegister(terminal_handler_fast_dispatch_hint, code.getCurr(), "a64_terminal_handler_fast_dispatch_hint");
    }
}

void A64EmitX64::EmitA64SetCheckBit(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg8 to_store = ctx.reg_alloc.UseGpr(args[0]).cvt8();
    code.mov(code.byte[r15 + offsetof(A64JitState, check_bit)], to_store);
}

void A64EmitX64::EmitA64GetCFlag(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A64JitState, CPSR_nzcv)]);
    code.shr(result, 29);
    code.and_(result, 1);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64SetNZCV(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 to_store = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
    code.and_(to_store, 0b11000001'00000001);
    code.imul(to_store, to_store, 0b00010000'00100001);
    code.shl(to_store, 16);
    code.and_(to_store, 0xF0000000);
    code.mov(dword[r15 + offsetof(A64JitState, CPSR_nzcv)], to_store);
}

void A64EmitX64::EmitA64GetW(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();

    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetX(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();

    Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr();
    code.mov(result, qword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetS(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = qword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code.movd(result, addr);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetD(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = qword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code.movq(result, addr);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetQ(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = xword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code.movaps(result, addr);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetSP(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr();
    code.mov(result, qword[r15 + offsetof(A64JitState, sp)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetFPCR(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A64JitState, fpcr)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

static u32 GetFPSRImpl(A64JitState* jit_state) {
    return jit_state->GetFpsr();
}

void A64EmitX64::EmitA64GetFPSR(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(inst);
    code.mov(code.ABI_PARAM1, code.r15);
    code.stmxcsr(code.dword[code.r15 + offsetof(A64JitState, guest_MXCSR)]);
    code.CallFunction(GetFPSRImpl);
}

void A64EmitX64::EmitA64SetW(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    auto addr = qword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)];
    if (args[1].FitsInImmediateS32()) {
        code.mov(addr, args[1].GetImmediateS32());
    } else {
        // TODO: zext tracking, xmm variant
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseScratchGpr(args[1]);
        code.mov(to_store.cvt32(), to_store.cvt32());
        code.mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64SetX(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    auto addr = qword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)];
    if (args[1].FitsInImmediateS32()) {
        code.mov(addr, args[1].GetImmediateS32());
    } else if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
        code.movq(addr, to_store);
    } else {
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(args[1]);
        code.mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64SetS(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = xword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = ctx.reg_alloc.ScratchXmm();
    // TODO: Optimize
    code.pxor(tmp, tmp);
    code.movss(tmp, to_store);
    code.movaps(addr, tmp);
}

void A64EmitX64::EmitA64SetD(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = xword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm to_store = ctx.reg_alloc.UseScratchXmm(args[1]);
    code.movq(to_store, to_store); // TODO: Remove when able
    code.movaps(addr, to_store);
}

void A64EmitX64::EmitA64SetQ(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = xword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
    code.movaps(addr, to_store);
}

void A64EmitX64::EmitA64SetSP(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto addr = qword[r15 + offsetof(A64JitState, sp)];
    if (args[0].FitsInImmediateS32()) {
        code.mov(addr, args[0].GetImmediateS32());
    } else if (args[0].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[0]);
        code.movq(addr, to_store);
    } else {
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(args[0]);
        code.mov(addr, to_store);
    }
}

static void SetFPCRImpl(A64JitState* jit_state, u32 value) {
    jit_state->SetFpcr(value);
}

void A64EmitX64::EmitA64SetFPCR(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0]);
    code.mov(code.ABI_PARAM1, code.r15);
    code.CallFunction(SetFPCRImpl);
    code.ldmxcsr(code.dword[code.r15 + offsetof(A64JitState, guest_MXCSR)]);
}

static void SetFPSRImpl(A64JitState* jit_state, u32 value) {
    jit_state->SetFpsr(value);
}

void A64EmitX64::EmitA64SetFPSR(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0]);
    code.mov(code.ABI_PARAM1, code.r15);
    code.CallFunction(SetFPSRImpl);
    code.ldmxcsr(code.dword[code.r15 + offsetof(A64JitState, guest_MXCSR)]);
}

void A64EmitX64::EmitA64OrQC(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (args[0].IsImmediate()) {
        if (!args[0].GetImmediateU1())
            return;

        code.mov(code.byte[code.r15 + offsetof(A64JitState, fpsr_qc)], u8(1));
        return;
    }

    const Xbyak::Reg8 to_store = ctx.reg_alloc.UseGpr(args[0]).cvt8();
    code.or_(code.byte[code.r15 + offsetof(A64JitState, fpsr_qc)], to_store);
}

void A64EmitX64::EmitA64SetPC(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto addr = qword[r15 + offsetof(A64JitState, pc)];
    if (args[0].FitsInImmediateS32()) {
        code.mov(addr, args[0].GetImmediateS32());
    } else if (args[0].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[0]);
        code.movq(addr, to_store);
    } else {
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(args[0]);
        code.mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64CallSupervisor(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(nullptr);
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    u32 imm = args[0].GetImmediateU32();
    Devirtualize<&A64::UserCallbacks::CallSVC>(conf.callbacks).EmitCall(code,
        [&](RegList param) {
            code.mov(param[0], imm);
        });
    // The kernel would have to execute ERET to get here, which would clear exclusive state.
    code.mov(code.byte[r15 + offsetof(A64JitState, exclusive_state)], u8(0));
}

void A64EmitX64::EmitA64ExceptionRaised(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(nullptr);
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate() && args[1].IsImmediate());
    u64 pc = args[0].GetImmediateU64();
    u64 exception = args[1].GetImmediateU64();
    Devirtualize<&A64::UserCallbacks::ExceptionRaised>(conf.callbacks).EmitCall(code,
        [&](RegList param) {
            code.mov(param[0], pc);
            code.mov(param[1], exception);
        });
}

void A64EmitX64::EmitA64DataCacheOperationRaised(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, args[0], args[1]);
    Devirtualize<&A64::UserCallbacks::DataCacheOperationRaised>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64DataSynchronizationBarrier(A64EmitContext&, IR::Inst*) {
    code.mfence();
}

void A64EmitX64::EmitA64DataMemoryBarrier(A64EmitContext&, IR::Inst*) {
    code.lfence();
}

void A64EmitX64::EmitA64InstructionSynchronizationBarrier(A64EmitContext& ctx, IR::Inst* ) {
    ctx.reg_alloc.HostCall(nullptr);

    code.mov(code.ABI_PARAM1, reinterpret_cast<u64>(jit_interface));
    code.CallFunction(static_cast<void(*)(A64::Jit*)>([](A64::Jit* jit) {
        jit->ClearCache();
    }));
}

void A64EmitX64::EmitA64GetCNTFRQ(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, conf.cntfrq_el0);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetCNTPCT(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(inst);
    code.UpdateTicks();
    Devirtualize<&A64::UserCallbacks::GetCNTPCT>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64GetCTR(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, conf.ctr_el0);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetDCZID(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, conf.dczid_el0);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetTPIDR(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr();
    if (conf.tpidr_el0) {
        code.mov(result, u64(conf.tpidr_el0));
        code.mov(result, qword[result]);
    } else {
        code.xor_(result.cvt32(), result.cvt32());
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetTPIDRRO(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr();
    if (conf.tpidrro_el0) {
        code.mov(result, u64(conf.tpidrro_el0));
        code.mov(result, qword[result]);
    } else {
        code.xor_(result.cvt32(), result.cvt32());
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64SetTPIDR(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 value = ctx.reg_alloc.UseGpr(args[0]);
    Xbyak::Reg64 addr = ctx.reg_alloc.ScratchGpr();
    if (conf.tpidr_el0) {
        code.mov(addr, u64(conf.tpidr_el0));
        code.mov(qword[addr], value);
    }
}

void A64EmitX64::EmitA64ClearExclusive(A64EmitContext&, IR::Inst*) {
    code.mov(code.byte[r15 + offsetof(A64JitState, exclusive_state)], u8(0));
}

void A64EmitX64::EmitA64SetExclusive(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.global_monitor) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);

        code.mov(code.byte[r15 + offsetof(A64JitState, exclusive_state)], u8(1));
        code.mov(code.ABI_PARAM1, reinterpret_cast<u64>(&conf));
        code.CallFunction(static_cast<void(*)(A64::UserConfig&, u64, u8)>(
            [](A64::UserConfig& conf, u64 vaddr, u8 size) {
                conf.global_monitor->Mark(conf.processor_id, vaddr, size);
            }
        ));

        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    Xbyak::Reg64 address = ctx.reg_alloc.UseGpr(args[0]);

    code.mov(code.byte[r15 + offsetof(A64JitState, exclusive_state)], u8(1));
    code.mov(qword[r15 + offsetof(A64JitState, exclusive_address)], address);
}

static Xbyak::RegExp EmitVAddrLookup(BlockOfCode& code, A64EmitContext& ctx, Xbyak::Label& abort, Xbyak::Reg64 vaddr, std::optional<Xbyak::Reg64> arg_scratch = {}) {
    constexpr size_t page_bits = 12;
    constexpr size_t page_size = 1 << page_bits;
    const size_t valid_page_index_bits = ctx.conf.page_table_address_space_bits - page_bits;
    const size_t unused_top_bits = 64 - ctx.conf.page_table_address_space_bits;

    Xbyak::Reg64 page_table = arg_scratch ? *arg_scratch : ctx.reg_alloc.ScratchGpr();
    Xbyak::Reg64 tmp = ctx.reg_alloc.ScratchGpr();
    code.mov(page_table, reinterpret_cast<u64>(ctx.conf.page_table));
    code.mov(tmp, vaddr);
    if (unused_top_bits == 0) {
        code.shr(tmp, int(page_bits));
    } else if (ctx.conf.silently_mirror_page_table) {
        if (valid_page_index_bits >= 32) {
            code.shl(tmp, int(unused_top_bits));
            code.shr(tmp, int(unused_top_bits + page_bits));
        } else {
            code.shr(tmp, int(page_bits));
            code.and_(tmp, u32((1 << valid_page_index_bits) - 1));
        }
    } else {
        ASSERT(valid_page_index_bits < 32);
        code.shr(tmp, int(page_bits));
        code.test(tmp, u32(-(1 << valid_page_index_bits)));
        code.jnz(abort, code.T_NEAR);
    }
    code.mov(page_table, qword[page_table + tmp * sizeof(void*)]);
    code.test(page_table, page_table);
    code.jz(abort, code.T_NEAR);
    code.mov(tmp, vaddr);
    code.and_(tmp, static_cast<u32>(page_size - 1));
    return page_table + tmp;
}

void A64EmitX64::EmitDirectPageTableMemoryRead(A64EmitContext& ctx, IR::Inst* inst, size_t bitsize) {
    Xbyak::Label abort, end;

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 vaddr = ctx.reg_alloc.UseGpr(args[0]);
    Xbyak::Reg64 value = ctx.reg_alloc.ScratchGpr();

    auto src_ptr = EmitVAddrLookup(code, ctx, abort, vaddr, value);
    switch (bitsize) {
    case 8:
        code.movzx(value.cvt32(), code.byte[src_ptr]);
        break;
    case 16:
        code.movzx(value.cvt32(), word[src_ptr]);
        break;
    case 32:
        code.mov(value.cvt32(), dword[src_ptr]);
        break;
    case 64:
        code.mov(value, qword[src_ptr]);
        break;
    }
    code.L(end);

    code.SwitchToFarCode();
    code.L(abort);
    code.call(read_fallbacks[std::make_tuple(bitsize, vaddr.getIdx(), value.getIdx())]);
    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();

    ctx.reg_alloc.DefineValue(inst, value);
}

void A64EmitX64::EmitDirectPageTableMemoryWrite(A64EmitContext& ctx, IR::Inst* inst, size_t bitsize) {
    Xbyak::Label abort, end;

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 vaddr = ctx.reg_alloc.UseGpr(args[0]);
    Xbyak::Reg64 value = ctx.reg_alloc.UseGpr(args[1]);

    auto dest_ptr = EmitVAddrLookup(code, ctx, abort, vaddr);
    switch (bitsize) {
    case 8:
        code.mov(code.byte[dest_ptr], value.cvt8());
        break;
    case 16:
        code.mov(word[dest_ptr], value.cvt16());
        break;
    case 32:
        code.mov(dword[dest_ptr], value.cvt32());
        break;
    case 64:
        code.mov(qword[dest_ptr], value);
        break;
    }
    code.L(end);

    code.SwitchToFarCode();
    code.L(abort);
    code.call(write_fallbacks[std::make_tuple(bitsize, vaddr.getIdx(), value.getIdx())]);
    code.jmp(end, code.T_NEAR);
    code.SwitchToNearCode();
}

void A64EmitX64::EmitA64ReadMemory8(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryRead(ctx, inst, 8);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(inst, {}, args[0]);
    Devirtualize<&A64::UserCallbacks::MemoryRead8>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64ReadMemory16(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryRead(ctx, inst, 16);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(inst, {}, args[0]);
    Devirtualize<&A64::UserCallbacks::MemoryRead16>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64ReadMemory32(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryRead(ctx, inst, 32);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(inst, {}, args[0]);
    Devirtualize<&A64::UserCallbacks::MemoryRead32>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64ReadMemory64(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryRead(ctx, inst, 64);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(inst, {}, args[0]);
    Devirtualize<&A64::UserCallbacks::MemoryRead64>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64ReadMemory128(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        Xbyak::Label abort, end;

        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        Xbyak::Reg64 vaddr = ctx.reg_alloc.UseGpr(args[0]);
        Xbyak::Xmm value = ctx.reg_alloc.ScratchXmm();

        auto src_ptr = EmitVAddrLookup(code, ctx, abort, vaddr);
        code.movups(value, xword[src_ptr]);
        code.L(end);

        code.SwitchToFarCode();
        code.L(abort);
        code.call(read_fallbacks[std::make_tuple(128, vaddr.getIdx(), value.getIdx())]);
        code.jmp(end, code.T_NEAR);
        code.SwitchToNearCode();

        ctx.reg_alloc.DefineValue(inst, value);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0]);
    code.CallFunction(memory_read_128);
    ctx.reg_alloc.DefineValue(inst, xmm1);
}

void A64EmitX64::EmitA64WriteMemory8(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryWrite(ctx, inst, 8);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    Devirtualize<&A64::UserCallbacks::MemoryWrite8>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64WriteMemory16(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryWrite(ctx, inst, 16);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    Devirtualize<&A64::UserCallbacks::MemoryWrite16>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64WriteMemory32(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryWrite(ctx, inst, 32);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    Devirtualize<&A64::UserCallbacks::MemoryWrite32>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64WriteMemory64(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        EmitDirectPageTableMemoryWrite(ctx, inst, 64);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    Devirtualize<&A64::UserCallbacks::MemoryWrite64>(conf.callbacks).EmitCall(code);
}

void A64EmitX64::EmitA64WriteMemory128(A64EmitContext& ctx, IR::Inst* inst) {
    if (conf.page_table) {
        Xbyak::Label abort, end;

        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        Xbyak::Reg64 vaddr = ctx.reg_alloc.UseGpr(args[0]);
        Xbyak::Xmm value = ctx.reg_alloc.UseXmm(args[1]);

        auto dest_ptr = EmitVAddrLookup(code, ctx, abort, vaddr);
        code.movups(xword[dest_ptr], value);
        code.L(end);

        code.SwitchToFarCode();
        code.L(abort);
        code.call(write_fallbacks[std::make_tuple(128, vaddr.getIdx(), value.getIdx())]);
        code.jmp(end, code.T_NEAR);
        code.SwitchToNearCode();
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.Use(args[0], ABI_PARAM2);
    ctx.reg_alloc.Use(args[1], HostLoc::XMM1);
    ctx.reg_alloc.EndOfAllocScope();
    ctx.reg_alloc.HostCall(nullptr);
    code.CallFunction(memory_write_128);
}

void A64EmitX64::EmitExclusiveWrite(A64EmitContext& ctx, IR::Inst* inst, size_t bitsize) {
    if (conf.global_monitor) {
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);

        if (bitsize != 128) {
            ctx.reg_alloc.HostCall(inst, {}, args[0], args[1]);
        } else {
            ctx.reg_alloc.Use(args[0], ABI_PARAM2);
            ctx.reg_alloc.Use(args[1], HostLoc::XMM1);
            ctx.reg_alloc.EndOfAllocScope();
            ctx.reg_alloc.HostCall(inst);
        }

        Xbyak::Label end;

        code.mov(code.ABI_RETURN, u32(1));
        code.cmp(code.byte[r15 + offsetof(A64JitState, exclusive_state)], u8(0));
        code.je(end);
        code.mov(code.ABI_PARAM1, reinterpret_cast<u64>(&conf));
        switch (bitsize) {
        case 8:
            code.CallFunction(static_cast<u32(*)(A64::UserConfig&, u64, u8)>(
                [](A64::UserConfig& conf, u64 vaddr, u8 value) -> u32 {
                    return conf.global_monitor->DoExclusiveOperation(conf.processor_id, vaddr, 1, [&]{
                        conf.callbacks->MemoryWrite8(vaddr, value);
                    }) ? 0 : 1;
                }
            ));
            break;
        case 16:
            code.CallFunction(static_cast<u32(*)(A64::UserConfig&, u64, u16)>(
                [](A64::UserConfig& conf, u64 vaddr, u16 value) -> u32 {
                    return conf.global_monitor->DoExclusiveOperation(conf.processor_id, vaddr, 2, [&]{
                        conf.callbacks->MemoryWrite16(vaddr, value);
                    }) ? 0 : 1;
                }
            ));
            break;
        case 32:
            code.CallFunction(static_cast<u32(*)(A64::UserConfig&, u64, u32)>(
                [](A64::UserConfig& conf, u64 vaddr, u32 value) -> u32 {
                    return conf.global_monitor->DoExclusiveOperation(conf.processor_id, vaddr, 4, [&]{
                        conf.callbacks->MemoryWrite32(vaddr, value);
                    }) ? 0 : 1;
                }
            ));
            break;
        case 64:
            code.CallFunction(static_cast<u32(*)(A64::UserConfig&, u64, u64)>(
                [](A64::UserConfig& conf, u64 vaddr, u64 value) -> u32 {
                    return conf.global_monitor->DoExclusiveOperation(conf.processor_id, vaddr, 8, [&]{
                        conf.callbacks->MemoryWrite64(vaddr, value);
                    }) ? 0 : 1;
                }
            ));
            break;
        case 128:
            code.sub(rsp, 16 + ABI_SHADOW_SPACE);
            code.lea(code.ABI_PARAM3, ptr[rsp + ABI_SHADOW_SPACE]);
            code.movaps(xword[code.ABI_PARAM3], xmm1);
            code.CallFunction(static_cast<u32(*)(A64::UserConfig&, u64, A64::Vector&)>(
                [](A64::UserConfig& conf, u64 vaddr, A64::Vector& value) -> u32 {
                    return conf.global_monitor->DoExclusiveOperation(conf.processor_id, vaddr, 16, [&]{
                        conf.callbacks->MemoryWrite128(vaddr, value);
                    }) ? 0 : 1;
                }
            ));
            code.add(rsp, 16 + ABI_SHADOW_SPACE);
            break;
        default:
            UNREACHABLE();
        }
        code.L(end);

        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 vaddr = ctx.reg_alloc.UseGpr(args[0]);
    int value_idx = bitsize != 128
                  ? ctx.reg_alloc.UseGpr(args[1]).getIdx()
                  : ctx.reg_alloc.UseXmm(args[1]).getIdx();

    Xbyak::Label end;
    Xbyak::Reg32 passed = ctx.reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg64 tmp = ctx.reg_alloc.ScratchGpr();

    code.mov(passed, u32(1));
    code.cmp(code.byte[r15 + offsetof(A64JitState, exclusive_state)], u8(0));
    code.je(end);
    code.mov(tmp, vaddr);
    code.xor_(tmp, qword[r15 + offsetof(A64JitState, exclusive_address)]);
    code.test(tmp, static_cast<u32>(A64JitState::RESERVATION_GRANULE_MASK & 0xFFFF'FFFF));
    code.jne(end);
    code.mov(code.byte[r15 + offsetof(A64JitState, exclusive_state)], u8(0));
    code.call(write_fallbacks[std::make_tuple(bitsize, vaddr.getIdx(), value_idx)]);
    code.xor_(passed, passed);
    code.L(end);

    ctx.reg_alloc.DefineValue(inst, passed);
}

void A64EmitX64::EmitA64ExclusiveWriteMemory8(A64EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWrite(ctx, inst, 8);
}

void A64EmitX64::EmitA64ExclusiveWriteMemory16(A64EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWrite(ctx, inst, 16);
}

void A64EmitX64::EmitA64ExclusiveWriteMemory32(A64EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWrite(ctx, inst, 32);
}

void A64EmitX64::EmitA64ExclusiveWriteMemory64(A64EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWrite(ctx, inst, 64);
}

void A64EmitX64::EmitA64ExclusiveWriteMemory128(A64EmitContext& ctx, IR::Inst* inst) {
    EmitExclusiveWrite(ctx, inst, 128);
}

std::string A64EmitX64::LocationDescriptorToFriendlyName(const IR::LocationDescriptor& ir_descriptor) const {
    const A64::LocationDescriptor descriptor{ir_descriptor};
    return fmt::format("a64_{:016X}_fpcr{:08X}",
                       descriptor.PC(),
                       descriptor.FPCR().Value());
}

void A64EmitX64::EmitTerminalImpl(IR::Term::Interpret terminal, IR::LocationDescriptor) {
    code.SwitchMxcsrOnExit();
    Devirtualize<&A64::UserCallbacks::InterpreterFallback>(conf.callbacks).EmitCall(code,
        [&](RegList param) {
            code.mov(param[0], A64::LocationDescriptor{terminal.next}.PC());
            code.mov(qword[r15 + offsetof(A64JitState, pc)], param[0]);
            code.mov(param[1].cvt32(), terminal.num_instructions);
        });
    code.ReturnFromRunCode(true); // TODO: Check cycles
}

void A64EmitX64::EmitTerminalImpl(IR::Term::ReturnToDispatch, IR::LocationDescriptor) {
    code.ReturnFromRunCode();
}

void A64EmitX64::EmitTerminalImpl(IR::Term::LinkBlock terminal, IR::LocationDescriptor) {
    code.cmp(qword[r15 + offsetof(A64JitState, cycles_remaining)], 0);

    patch_information[terminal.next].jg.emplace_back(code.getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJg(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJg(terminal.next);
    }
    code.mov(rax, A64::LocationDescriptor{terminal.next}.PC());
    code.mov(qword[r15 + offsetof(A64JitState, pc)], rax);
    code.ForceReturnFromRunCode();
}

void A64EmitX64::EmitTerminalImpl(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor) {
    patch_information[terminal.next].jmp.emplace_back(code.getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJmp(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJmp(terminal.next);
    }
}

void A64EmitX64::EmitTerminalImpl(IR::Term::PopRSBHint, IR::LocationDescriptor) {
    code.jmp(terminal_handler_pop_rsb_hint);
}

void A64EmitX64::EmitTerminalImpl(IR::Term::FastDispatchHint, IR::LocationDescriptor) {
    if (conf.enable_fast_dispatch) {
        code.jmp(terminal_handler_fast_dispatch_hint);
    } else {
        code.ReturnFromRunCode();
    }
}

void A64EmitX64::EmitTerminalImpl(IR::Term::If terminal, IR::LocationDescriptor initial_location) {
    switch (terminal.if_) {
    case IR::Cond::AL:
    case IR::Cond::NV:
        EmitTerminal(terminal.then_, initial_location);
        break;
    default:
        Xbyak::Label pass = EmitCond(terminal.if_);
        EmitTerminal(terminal.else_, initial_location);
        code.L(pass);
        EmitTerminal(terminal.then_, initial_location);
        break;
    }
}

void A64EmitX64::EmitTerminalImpl(IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location) {
    Xbyak::Label fail;
    code.cmp(code.byte[r15 + offsetof(A64JitState, check_bit)], u8(0));
    code.jz(fail);
    EmitTerminal(terminal.then_, initial_location);
    code.L(fail);
    EmitTerminal(terminal.else_, initial_location);
}

void A64EmitX64::EmitTerminalImpl(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location) {
    code.cmp(code.byte[r15 + offsetof(A64JitState, halt_requested)], u8(0));
    code.jne(code.GetForceReturnFromRunCodeAddress());
    EmitTerminal(terminal.else_, initial_location);
}

void A64EmitX64::EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code.getCurr();
    if (target_code_ptr) {
        code.jg(target_code_ptr);
    } else {
        code.mov(rax, A64::LocationDescriptor{target_desc}.PC());
        code.mov(qword[r15 + offsetof(A64JitState, pc)], rax);
        code.jg(code.GetReturnFromRunCodeAddress());
    }
    code.EnsurePatchLocationSize(patch_location, 30); // TODO: Reduce size
}

void A64EmitX64::EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code.getCurr();
    if (target_code_ptr) {
        code.jmp(target_code_ptr);
    } else {
        code.mov(rax, A64::LocationDescriptor{target_desc}.PC());
        code.mov(qword[r15 + offsetof(A64JitState, pc)], rax);
        code.jmp(code.GetReturnFromRunCodeAddress());
    }
    code.EnsurePatchLocationSize(patch_location, 30); // TODO: Reduce size
}

void A64EmitX64::EmitPatchMovRcx(CodePtr target_code_ptr) {
    if (!target_code_ptr) {
        target_code_ptr = code.GetReturnFromRunCodeAddress();
    }
    const CodePtr patch_location = code.getCurr();
    code.mov(code.rcx, reinterpret_cast<u64>(target_code_ptr));
    code.EnsurePatchLocationSize(patch_location, 10);
}

} // namespace Dynarmic::BackendX64
