/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <unordered_map>
#include <unordered_set>

#include "backend_x64/a64_emit_x64.h"
#include "backend_x64/a64_jitstate.h"
#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/devirtualize.h"
#include "backend_x64/emit_x64.h"
#include "common/address_range.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
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

A64EmitContext::A64EmitContext(RegAlloc& reg_alloc, IR::Block& block)
    : EmitContext(reg_alloc, block) {}

A64::LocationDescriptor A64EmitContext::Location() const {
    return A64::LocationDescriptor{block.Location()};
}

bool A64EmitContext::FPSCR_RoundTowardsZero() const {
    return Location().FPCR().RMode() != A64::FPCR::RoundingMode::TowardsZero;
}

bool A64EmitContext::FPSCR_FTZ() const {
    return Location().FPCR().FZ();
}

bool A64EmitContext::FPSCR_DN() const {
    return Location().FPCR().DN();
}

A64EmitX64::A64EmitX64(BlockOfCode* code, A64::UserConfig conf)
    : EmitX64(code), conf(conf)
{
    code->PreludeComplete();
}

A64EmitX64::~A64EmitX64() = default;

A64EmitX64::BlockDescriptor A64EmitX64::Emit(IR::Block& block) {
    code->align();
    const u8* const entrypoint = code->getCurr();

    // Start emitting.
    EmitCondPrelude(block);

    RegAlloc reg_alloc{code, A64JitState::SpillCount, SpillToOpArg<A64JitState>};
    A64EmitContext ctx{reg_alloc, block};

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
            ASSERT_MSG(false, "Invalid opcode %zu", static_cast<size_t>(inst->GetOpcode()));
            break;
        }

        ctx.reg_alloc.EndOfAllocScope();
    }

    reg_alloc.AssertNoMoreUses();

    EmitAddCycles(block.CycleCount());
    EmitX64::EmitTerminal(block.GetTerminal(), block.Location());
    code->int3();

    const A64::LocationDescriptor descriptor{block.Location()};
    Patch(descriptor, entrypoint);

    const size_t size = static_cast<size_t>(code->getCurr() - entrypoint);
    const A64::LocationDescriptor end_location{block.EndLocation()};
    const auto range = boost::icl::discrete_interval<u64>::closed(descriptor.PC(), end_location.PC() - 1);
    A64EmitX64::BlockDescriptor block_desc{entrypoint, size};
    block_descriptors.emplace(descriptor.UniqueHash(), block_desc);
    block_ranges.AddRange(range, descriptor);

    return block_desc;
}

void A64EmitX64::ClearCache() {
    EmitX64::ClearCache();
    block_ranges.ClearCache();
}

void A64EmitX64::InvalidateCacheRanges(const boost::icl::interval_set<u64>& ranges) {
    InvalidateBasicBlocks(block_ranges.InvalidateRanges(ranges));
}

void A64EmitX64::EmitA64SetCheckBit(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg8 to_store = ctx.reg_alloc.UseGpr(args[0]).cvt8();
    code->mov(code->byte[r15 + offsetof(A64JitState, check_bit)], to_store);
}

void A64EmitX64::EmitA64GetCFlag(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(A64JitState, CPSR_nzcv)]);
    code->shr(result, 29);
    code->and_(result, 1);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64SetNZCV(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 to_store = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
    code->and_(to_store, 0b11000001'00000001);
    code->imul(to_store, to_store, 0b00010000'00100001);
    code->shl(to_store, 16);
    code->and_(to_store, 0xF0000000);
    code->mov(dword[r15 + offsetof(A64JitState, CPSR_nzcv)], to_store);
}

void A64EmitX64::EmitA64GetW(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();

    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetX(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();

    Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr();
    code->mov(result, qword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetD(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = qword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code->movq(result, addr);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetQ(A64EmitContext& ctx, IR::Inst* inst) {
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = code->xword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code->movaps(result, addr);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64GetSP(A64EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr();
    code->mov(result, qword[r15 + offsetof(A64JitState, sp)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A64EmitX64::EmitA64SetW(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    auto addr = qword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)];
    if (args[1].FitsInImmediateS32()) {
        code->mov(addr, args[1].GetImmediateS32());
    } else {
        // TODO: zext tracking, xmm variant
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseScratchGpr(args[1]);
        code->mov(to_store.cvt32(), to_store.cvt32());
        code->mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64SetX(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Reg reg = inst->GetArg(0).GetA64RegRef();
    auto addr = qword[r15 + offsetof(A64JitState, reg) + sizeof(u64) * static_cast<size_t>(reg)];
    if (args[1].FitsInImmediateS32()) {
        code->mov(addr, args[1].GetImmediateS32());
    } else if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
        code->movq(addr, to_store);
    } else {
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(args[1]);
        code->mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64SetD(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = code->xword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm to_store = ctx.reg_alloc.UseScratchXmm(args[1]);
    code->movq(to_store, to_store);
    code->movaps(addr, to_store);
}

void A64EmitX64::EmitA64SetQ(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A64::Vec vec = inst->GetArg(0).GetA64VecRef();
    auto addr = code->xword[r15 + offsetof(A64JitState, vec) + sizeof(u64) * 2 * static_cast<size_t>(vec)];

    Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
    code->movaps(addr, to_store);
}

void A64EmitX64::EmitA64SetSP(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto addr = qword[r15 + offsetof(A64JitState, sp)];
    if (args[0].FitsInImmediateU32()) {
        code->mov(addr, args[0].GetImmediateU32());
    } else if (args[0].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[0]);
        code->movq(addr, to_store);
    } else {
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(args[0]);
        code->mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64SetPC(A64EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto addr = qword[r15 + offsetof(A64JitState, pc)];
    if (args[0].FitsInImmediateU32()) {
        code->mov(addr, args[0].GetImmediateU32());
    } else if (args[0].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[0]);
        code->movq(addr, to_store);
    } else {
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(args[0]);
        code->mov(addr, to_store);
    }
}

void A64EmitX64::EmitA64CallSupervisor(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(nullptr);
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    u32 imm = args[0].GetImmediateU32();
    DEVIRT(conf.callbacks, &A64::UserCallbacks::CallSVC).EmitCall(code, [&](Xbyak::Reg64 param1) {
        code->mov(param1.cvt32(), imm);
    });
}

void A64EmitX64::EmitA64ExceptionRaised(A64EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(nullptr);
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate() && args[1].IsImmediate());
    u64 pc = args[0].GetImmediateU64();
    u64 exception = args[1].GetImmediateU64();
    DEVIRT(conf.callbacks, &A64::UserCallbacks::ExceptionRaised).EmitCall(code, [&](Xbyak::Reg64 param1, Xbyak::Reg64 param2) {
        code->mov(param1, pc);
        code->mov(param2, exception);
    });
}

void A64EmitX64::EmitA64ReadMemory8(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryRead8).EmitCall(code, [&](Xbyak::Reg64 vaddr) {
        ASSERT(vaddr == code->ABI_PARAM2);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(inst, {}, args[0]);
    });
}

void A64EmitX64::EmitA64ReadMemory16(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryRead16).EmitCall(code, [&](Xbyak::Reg64 vaddr) {
        ASSERT(vaddr == code->ABI_PARAM2);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(inst, {}, args[0]);
    });
}

void A64EmitX64::EmitA64ReadMemory32(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryRead32).EmitCall(code, [&](Xbyak::Reg64 vaddr) {
        ASSERT(vaddr == code->ABI_PARAM2);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(inst, {}, args[0]);
    });
}

void A64EmitX64::EmitA64ReadMemory64(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryRead64).EmitCall(code, [&](Xbyak::Reg64 vaddr) {
        ASSERT(vaddr == code->ABI_PARAM2);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(inst, {}, args[0]);
    });
}

void A64EmitX64::EmitA64ReadMemory128(A64EmitContext& ctx, IR::Inst* inst) {
#ifdef _WIN32
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    static_assert(ABI_SHADOW_SPACE >= 16);
    ctx.reg_alloc.HostCall(nullptr, {}, {}, args[0]);
    code->lea(code->ABI_PARAM2, ptr[rsp]);
    code->sub(rsp, ABI_SHADOW_SPACE);

    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryRead128).EmitCall(code, [&](Xbyak::Reg64 return_value, Xbyak::Reg64 vaddr) {
        ASSERT(return_value == code->ABI_PARAM2 && vaddr == code->ABI_PARAM3);
    });

    Xbyak::Xmm result = xmm0;
    code->movups(result, code->xword[code->ABI_RETURN]);
    code->add(rsp, ABI_SHADOW_SPACE);

    ctx.reg_alloc.DefineValue(inst, result);
#else
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryRead128).EmitCall(code, [&](Xbyak::Reg64 vaddr) {
        ASSERT(vaddr == code->ABI_PARAM2);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(nullptr, {}, args[0]);
    });
    Xbyak::Xmm result = xmm0;
    if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
        code->movq(result, code->ABI_RETURN);
        code->pinsrq(result, code->ABI_RETURN2, 1);
    } else {
        Xbyak::Xmm tmp = xmm1;
        code->movq(result, code->ABI_RETURN);
        code->movq(tmp, code->ABI_RETURN2);
        code->punpcklqdq(result, tmp);
    }
    ctx.reg_alloc.DefineValue(inst, result);
#endif
}

void A64EmitX64::EmitA64WriteMemory8(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryWrite8).EmitCall(code, [&](Xbyak::Reg64 vaddr, Xbyak::Reg64 value) {
        ASSERT(vaddr == code->ABI_PARAM2 && value == code->ABI_PARAM3);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    });
}

void A64EmitX64::EmitA64WriteMemory16(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryWrite16).EmitCall(code, [&](Xbyak::Reg64 vaddr, Xbyak::Reg64 value) {
        ASSERT(vaddr == code->ABI_PARAM2 && value == code->ABI_PARAM3);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    });
}

void A64EmitX64::EmitA64WriteMemory32(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryWrite32).EmitCall(code, [&](Xbyak::Reg64 vaddr, Xbyak::Reg64 value) {
        ASSERT(vaddr == code->ABI_PARAM2 && value == code->ABI_PARAM3);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    });
}

void A64EmitX64::EmitA64WriteMemory64(A64EmitContext& ctx, IR::Inst* inst) {
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryWrite64).EmitCall(code, [&](Xbyak::Reg64 vaddr, Xbyak::Reg64 value) {
        ASSERT(vaddr == code->ABI_PARAM2 && value == code->ABI_PARAM3);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    });
}

void A64EmitX64::EmitA64WriteMemory128(A64EmitContext& ctx, IR::Inst* inst) {
#ifdef _WIN32
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    static_assert(ABI_SHADOW_SPACE >= 16);
    ctx.reg_alloc.Use(args[0], ABI_PARAM2);
    Xbyak::Xmm xmm_value = ctx.reg_alloc.UseXmm(args[1]);
    ctx.reg_alloc.EndOfAllocScope();
    ctx.reg_alloc.HostCall(nullptr);
    code->lea(code->ABI_PARAM3, ptr[rsp]);
    code->sub(rsp, ABI_SHADOW_SPACE);
    code->movaps(code->xword[code->ABI_PARAM3], xmm_value);

    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryWrite128).EmitCall(code, [&](Xbyak::Reg64 vaddr, Xbyak::Reg64 value_ptr) {
        ASSERT(vaddr == code->ABI_PARAM2 && value_ptr == code->ABI_PARAM3);
    });

    code->add(rsp, ABI_SHADOW_SPACE);
#else
    DEVIRT(conf.callbacks, &A64::UserCallbacks::MemoryWrite128).EmitCall(code, [&](Xbyak::Reg64 vaddr, Xbyak::Reg64 value0, Xbyak::Reg64 value1) {
        ASSERT(vaddr == code->ABI_PARAM2 && value0 == code->ABI_PARAM3 && value1 == code->ABI_PARAM4);
        auto args = ctx.reg_alloc.GetArgumentInfo(inst);
        ctx.reg_alloc.Use(args[0], ABI_PARAM2);
        ctx.reg_alloc.ScratchGpr({ABI_PARAM3});
        ctx.reg_alloc.ScratchGpr({ABI_PARAM4});
        if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
            Xbyak::Xmm xmm_value = ctx.reg_alloc.UseXmm(args[1]);
            code->movq(code->ABI_PARAM3, xmm_value);
            code->pextrq(code->ABI_PARAM4, xmm_value, 1);
        } else {
            Xbyak::Xmm xmm_value = ctx.reg_alloc.UseScratchXmm(args[1]);
            code->movq(code->ABI_PARAM3, xmm_value);
            code->punpckhqdq(xmm_value, xmm_value);
            code->movq(code->ABI_PARAM4, xmm_value);
        }
        ctx.reg_alloc.EndOfAllocScope();
        ctx.reg_alloc.HostCall(nullptr);
    });
#endif
}

void A64EmitX64::EmitTerminalImpl(IR::Term::Interpret terminal, IR::LocationDescriptor) {
    code->SwitchMxcsrOnExit();
    DEVIRT(conf.callbacks, &A64::UserCallbacks::InterpreterFallback).EmitCall(code, [&](Xbyak::Reg64 param1, Xbyak::Reg64 param2) {
        code->mov(param1, A64::LocationDescriptor{terminal.next}.PC());
        code->mov(qword[r15 + offsetof(A64JitState, pc)], param1);
        code->mov(param2.cvt32(), terminal.num_instructions);
    });
    code->ReturnFromRunCode(true); // TODO: Check cycles
}

void A64EmitX64::EmitTerminalImpl(IR::Term::ReturnToDispatch, IR::LocationDescriptor) {
    code->ReturnFromRunCode();
}

void A64EmitX64::EmitTerminalImpl(IR::Term::LinkBlock terminal, IR::LocationDescriptor) {
    code->cmp(qword[r15 + offsetof(A64JitState, cycles_remaining)], 0);

    patch_information[terminal.next].jg.emplace_back(code->getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJg(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJg(terminal.next);
    }
    code->mov(rax, A64::LocationDescriptor{terminal.next}.PC());
    code->mov(qword[r15 + offsetof(A64JitState, pc)], rax);
    code->ForceReturnFromRunCode();
}

void A64EmitX64::EmitTerminalImpl(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor) {
    patch_information[terminal.next].jmp.emplace_back(code->getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJmp(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJmp(terminal.next);
    }
}

void A64EmitX64::EmitTerminalImpl(IR::Term::PopRSBHint, IR::LocationDescriptor initial_location) {
    EmitTerminalImpl(IR::Term::ReturnToDispatch{}, initial_location);
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
        code->L(pass);
        EmitTerminal(terminal.then_, initial_location);
        break;
    }
}

void A64EmitX64::EmitTerminalImpl(IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location) {
    Xbyak::Label fail;
    code->cmp(code->byte[r15 + offsetof(A64JitState, check_bit)], u8(0));
    code->jz(fail);
    EmitTerminal(terminal.then_, initial_location);
    code->L(fail);
    EmitTerminal(terminal.else_, initial_location);
}

void A64EmitX64::EmitTerminalImpl(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location) {
    code->cmp(code->byte[r15 + offsetof(A64JitState, halt_requested)], u8(0));
    code->jne(code->GetForceReturnFromRunCodeAddress());
    EmitTerminal(terminal.else_, initial_location);
}

void A64EmitX64::EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code->getCurr();
    if (target_code_ptr) {
        code->jg(target_code_ptr);
    } else {
        code->mov(rax, A64::LocationDescriptor{target_desc}.PC());
        code->mov(qword[r15 + offsetof(A64JitState, pc)], rax);
        code->jg(code->GetReturnFromRunCodeAddress());
    }
    code->EnsurePatchLocationSize(patch_location, 30); // TODO: Reduce size
}

void A64EmitX64::EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code->getCurr();
    if (target_code_ptr) {
        code->jmp(target_code_ptr);
    } else {
        code->mov(rax, A64::LocationDescriptor{target_desc}.PC());
        code->mov(qword[r15 + offsetof(A64JitState, pc)], rax);
        code->jmp(code->GetReturnFromRunCodeAddress());
    }
    code->EnsurePatchLocationSize(patch_location, 30); // TODO: Reduce size
}

void A64EmitX64::EmitPatchMovRcx(CodePtr target_code_ptr) {
    if (!target_code_ptr) {
        target_code_ptr = code->GetReturnFromRunCodeAddress();
    }
    const CodePtr patch_location = code->getCurr();
    code->mov(code->rcx, reinterpret_cast<u64>(target_code_ptr));
    code->EnsurePatchLocationSize(patch_location, 10);
}

} // namespace Dynarmic::BackendX64
