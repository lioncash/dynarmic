/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <fmt/ostream.h>

#include <dynarmic/A32/coprocessor.h>

#include "backend/x64/a32_emit_x64.h"
#include "backend/x64/a32_jitstate.h"
#include "backend/x64/abi.h"
#include "backend/x64/block_of_code.h"
#include "backend/x64/devirtualize.h"
#include "backend/x64/emit_x64.h"
#include "common/address_range.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/variant_util.h"
#include "frontend/A32/location_descriptor.h"
#include "frontend/A32/types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

// TODO: Have ARM flags in host flags and not have them use up GPR registers unless necessary.
// TODO: Actually implement that proper instruction selector you've always wanted to sweetheart.

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;

static Xbyak::Address MJitStateReg(A32::Reg reg) {
    return dword[r15 + offsetof(A32JitState, Reg) + sizeof(u32) * static_cast<size_t>(reg)];
}

static Xbyak::Address MJitStateExtReg(A32::ExtReg reg) {
    if (A32::IsSingleExtReg(reg)) {
        size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::S0);
        return dword[r15 + offsetof(A32JitState, ExtReg) + sizeof(u32) * index];
    }
    if (A32::IsDoubleExtReg(reg)) {
        size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::D0);
        return qword[r15 + offsetof(A32JitState, ExtReg) + sizeof(u64) * index];
    }
    ASSERT_MSG(false, "Should never happen.");
}

A32EmitContext::A32EmitContext(RegAlloc& reg_alloc, IR::Block& block)
    : EmitContext(reg_alloc, block) {}

A32::LocationDescriptor A32EmitContext::Location() const {
    return A32::LocationDescriptor{block.Location()};
}

FP::RoundingMode A32EmitContext::FPSCR_RMode() const {
    return Location().FPSCR().RMode();
}

u32 A32EmitContext::FPCR() const {
    return Location().FPSCR().Value();
}

bool A32EmitContext::FPSCR_FTZ() const {
    return Location().FPSCR().FTZ();
}

bool A32EmitContext::FPSCR_DN() const {
    return Location().FPSCR().DN();
}

A32EmitX64::A32EmitX64(BlockOfCode& code, A32::UserConfig config, A32::Jit* jit_interface)
    : EmitX64(code), config(std::move(config)), jit_interface(jit_interface)
{
    GenMemoryAccessors();
    code.PreludeComplete();
}

A32EmitX64::~A32EmitX64() = default;

A32EmitX64::BlockDescriptor A32EmitX64::Emit(IR::Block& block) {
    code.align();
    const u8* const entrypoint = code.getCurr();

    // Start emitting.
    EmitCondPrelude(block);

    RegAlloc reg_alloc{code, A32JitState::SpillCount, SpillToOpArg<A32JitState>};
    A32EmitContext ctx{reg_alloc, block};

    for (auto iter = block.begin(); iter != block.end(); ++iter) {
        IR::Inst* inst = &*iter;

        // Call the relevant Emit* member function.
        switch (inst->GetOpcode()) {

#define OPCODE(name, type, ...)                 \
        case IR::Opcode::name:                  \
            A32EmitX64::Emit##name(ctx, inst);  \
            break;
#define A32OPC(name, type, ...)                    \
        case IR::Opcode::A32##name:                \
            A32EmitX64::EmitA32##name(ctx, inst);  \
            break;
#define A64OPC(...)
#include "frontend/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC

        default:
            ASSERT_MSG(false, "Invalid opcode: {}", inst->GetOpcode());
            break;
        }

        reg_alloc.EndOfAllocScope();
    }

    reg_alloc.AssertNoMoreUses();

    EmitAddCycles(block.CycleCount());
    EmitX64::EmitTerminal(block.GetTerminal(), block.Location());
    code.int3();

    const A32::LocationDescriptor descriptor{block.Location()};
    Patch(descriptor, entrypoint);

    const size_t size = static_cast<size_t>(code.getCurr() - entrypoint);
    const A32::LocationDescriptor end_location{block.EndLocation()};
    const auto range = boost::icl::discrete_interval<u32>::closed(descriptor.PC(), end_location.PC() - 1);
    A32EmitX64::BlockDescriptor block_desc{entrypoint, size};
    block_descriptors.emplace(descriptor.UniqueHash(), block_desc);
    block_ranges.AddRange(range, descriptor);

    return block_desc;
}

void A32EmitX64::ClearCache() {
    EmitX64::ClearCache();
    block_ranges.ClearCache();
}

void A32EmitX64::InvalidateCacheRanges(const boost::icl::interval_set<u32>& ranges) {
    InvalidateBasicBlocks(block_ranges.InvalidateRanges(ranges));
}

void A32EmitX64::GenMemoryAccessors() {
    code.align();
    read_memory_8 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryRead8>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();

    code.align();
    read_memory_16 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryRead16>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();

    code.align();
    read_memory_32 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryRead32>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();

    code.align();
    read_memory_64 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryRead64>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();

    code.align();
    write_memory_8 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryWrite8>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();

    code.align();
    write_memory_16 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryWrite16>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();

    code.align();
    write_memory_32 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryWrite32>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();

    code.align();
    write_memory_64 = code.getCurr<const void*>();
    ABI_PushCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    Devirtualize<&A32::UserCallbacks::MemoryWrite64>(config.callbacks).EmitCall(code);
    ABI_PopCallerSaveRegistersAndAdjustStackExcept(code, ABI_RETURN);
    code.ret();
}

void A32EmitX64::EmitA32GetRegister(A32EmitContext& ctx, IR::Inst* inst) {
    A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, MJitStateReg(reg));
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32GetExtendedRegister32(A32EmitContext& ctx, IR::Inst* inst) {
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsSingleExtReg(reg));

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code.movss(result, MJitStateExtReg(reg));
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32GetExtendedRegister64(A32EmitContext& ctx, IR::Inst* inst) {
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg));

    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code.movsd(result, MJitStateExtReg(reg));
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetRegister(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A32::Reg reg = inst->GetArg(0).GetA32RegRef();
    if (args[1].IsImmediate()) {
        code.mov(MJitStateReg(reg), args[1].GetImmediateU32());
    } else if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
        code.movd(MJitStateReg(reg), to_store);
    } else {
        Xbyak::Reg32 to_store = ctx.reg_alloc.UseGpr(args[1]).cvt32();
        code.mov(MJitStateReg(reg), to_store);
    }
}

void A32EmitX64::EmitA32SetExtendedRegister32(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsSingleExtReg(reg));
    if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
        code.movss(MJitStateExtReg(reg), to_store);
    } else {
        Xbyak::Reg32 to_store = ctx.reg_alloc.UseGpr(args[1]).cvt32();
        code.mov(MJitStateExtReg(reg), to_store);
    }
}

void A32EmitX64::EmitA32SetExtendedRegister64(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg));
    if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[1]);
        code.movsd(MJitStateExtReg(reg), to_store);
    } else {
        Xbyak::Reg64 to_store = ctx.reg_alloc.UseGpr(args[1]);
        code.mov(MJitStateExtReg(reg), to_store);
    }
}

static u32 GetCpsrImpl(A32JitState* jit_state) {
    return jit_state->Cpsr();
}

void A32EmitX64::EmitA32GetCpsr(A32EmitContext& ctx, IR::Inst* inst) {
    if (code.DoesCpuSupport(Xbyak::util::Cpu::tBMI2)) {
        Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
        Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr().cvt32();

        // Here we observe that CPSR_et and CPSR_ge are right next to each other in memory,
        // so we load them both at the same time with one 64-bit read. This allows us to
        // extract all of their bits together at once with one pext.
        static_assert(offsetof(A32JitState, CPSR_et) + 4 == offsetof(A32JitState, CPSR_ge));
        code.mov(result.cvt64(), qword[r15 + offsetof(A32JitState, CPSR_et)]);
        code.mov(tmp.cvt64(), 0x8080808000000003ull);
        code.pext(result.cvt64(), result.cvt64(), tmp.cvt64());
        code.mov(tmp, 0x000f0220);
        code.pdep(result, result, tmp);
        code.mov(tmp, dword[r15 + offsetof(A32JitState, CPSR_q)]);
        code.shl(tmp, 27);
        code.or_(result, tmp);
        code.or_(result, dword[r15 + offsetof(A32JitState, CPSR_nzcv)]);
        code.or_(result, dword[r15 + offsetof(A32JitState, CPSR_jaifm)]);

        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ctx.reg_alloc.HostCall(inst);
        code.mov(code.ABI_PARAM1, code.r15);
        code.CallFunction(&GetCpsrImpl);
    }
}

static void SetCpsrImpl(u32 value, A32JitState* jit_state) {
    jit_state->SetCpsr(value);
}

void A32EmitX64::EmitA32SetCpsr(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tBMI2)) {
        Xbyak::Reg32 cpsr = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr().cvt32();

        // CPSR_q
        code.bt(cpsr, 27);
        code.setc(code.byte[r15 + offsetof(A32JitState, CPSR_q)]);

        // CPSR_nzcv
        code.mov(tmp, cpsr);
        code.and_(tmp, 0xF0000000);
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], tmp);

        // CPSR_jaifm
        code.mov(tmp, cpsr);
        code.and_(tmp, 0x07F0FDDF);
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_jaifm)], tmp);

        // CPSR_et and CPSR_ge
        static_assert(offsetof(A32JitState, CPSR_et) + 4 == offsetof(A32JitState, CPSR_ge));
        code.mov(tmp, 0x000f0220);
        code.pext(cpsr, cpsr, tmp);
        code.mov(tmp.cvt64(), 0x8080808000000003ull);
        code.pdep(cpsr.cvt64(), cpsr.cvt64(), tmp.cvt64());
        code.mov(tmp.cvt64(), cpsr.cvt64());
        code.shr(tmp.cvt64(), 7);
        code.sub(cpsr.cvt64(), tmp.cvt64());
        code.mov(qword[r15 + offsetof(A32JitState, CPSR_et)], cpsr.cvt64());
    } else {
        ctx.reg_alloc.HostCall(nullptr, args[0]);
        code.mov(code.ABI_PARAM2, code.r15);
        code.CallFunction(&SetCpsrImpl);
    }
}

void A32EmitX64::EmitA32SetCpsrNZCV(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        u32 imm = args[0].GetImmediateU32();

        code.mov(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], u32(imm & 0xF0000000));
    } else {
        Xbyak::Reg32 a = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

        code.and_(a, 0xF0000000);
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], a);
    }
}

void A32EmitX64::EmitA32SetCpsrNZCVQ(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        u32 imm = args[0].GetImmediateU32();

        code.mov(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], u32(imm & 0xF0000000));
        code.mov(code.byte[r15 + offsetof(A32JitState, CPSR_q)], u8((imm & 0x08000000) != 0 ? 1 : 0));
    } else {
        Xbyak::Reg32 a = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

        code.bt(a, 27);
        code.setc(code.byte[r15 + offsetof(A32JitState, CPSR_q)]);
        code.and_(a, 0xF0000000);
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], a);
    }
}

void A32EmitX64::EmitA32GetNFlag(A32EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A32JitState, CPSR_nzcv)]);
    code.shr(result, 31);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetNFlag(A32EmitContext& ctx, IR::Inst* inst) {
    constexpr size_t flag_bit = 31;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], flag_mask);
        } else {
            code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

        code.shl(to_store, flag_bit);
        code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32GetZFlag(A32EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A32JitState, CPSR_nzcv)]);
    code.shr(result, 30);
    code.and_(result, 1);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetZFlag(A32EmitContext& ctx, IR::Inst* inst) {
    constexpr size_t flag_bit = 30;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], flag_mask);
        } else {
            code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

        code.shl(to_store, flag_bit);
        code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32GetCFlag(A32EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A32JitState, CPSR_nzcv)]);
    code.shr(result, 29);
    code.and_(result, 1);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetCFlag(A32EmitContext& ctx, IR::Inst* inst) {
    constexpr size_t flag_bit = 29;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], flag_mask);
        } else {
            code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

        code.shl(to_store, flag_bit);
        code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32GetVFlag(A32EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A32JitState, CPSR_nzcv)]);
    code.shr(result, 28);
    code.and_(result, 1);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetVFlag(A32EmitContext& ctx, IR::Inst* inst) {
    constexpr size_t flag_bit = 28;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], flag_mask);
        } else {
            code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

        code.shl(to_store, flag_bit);
        code.and_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], ~flag_mask);
        code.or_(dword[r15 + offsetof(A32JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32OrQFlag(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1())
            code.mov(dword[r15 + offsetof(A32JitState, CPSR_q)], 1);
    } else {
        Xbyak::Reg8 to_store = ctx.reg_alloc.UseGpr(args[0]).cvt8();

        code.or_(code.byte[r15 + offsetof(A32JitState, CPSR_q)], to_store);
    }
}

void A32EmitX64::EmitA32GetGEFlags(A32EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Xmm result = ctx.reg_alloc.ScratchXmm();
    code.movd(result, dword[r15 + offsetof(A32JitState, CPSR_ge)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetGEFlags(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(!args[0].IsImmediate());

    if (args[0].IsInXmm()) {
        Xbyak::Xmm to_store = ctx.reg_alloc.UseXmm(args[0]);
        code.movd(dword[r15 + offsetof(A32JitState, CPSR_ge)], to_store);
    } else {
        Xbyak::Reg32 to_store = ctx.reg_alloc.UseGpr(args[0]).cvt32();
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_ge)], to_store);
    }
}

void A32EmitX64::EmitA32SetGEFlagsCompressed(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        u32 imm = args[0].GetImmediateU32();
        u32 ge = 0;
        ge |= Common::Bit<19>(imm) ? 0xFF000000 : 0;
        ge |= Common::Bit<18>(imm) ? 0x00FF0000 : 0;
        ge |= Common::Bit<17>(imm) ? 0x0000FF00 : 0;
        ge |= Common::Bit<16>(imm) ? 0x000000FF : 0;

        code.mov(dword[r15 + offsetof(A32JitState, CPSR_ge)], ge);
    } else if (code.DoesCpuSupport(Xbyak::util::Cpu::tBMI2)) {
        Xbyak::Reg32 a = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 b = ctx.reg_alloc.ScratchGpr().cvt32();

        code.mov(b, 0x01010101);
        code.shr(a, 16);
        code.pdep(a, a, b);
        code.imul(a, a, 0xFF);
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_ge)], a);
    } else {
        Xbyak::Reg32 a = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

        code.shr(a, 16);
        code.and_(a, 0xF);
        code.imul(a, a, 0x00204081);
        code.and_(a, 0x01010101);
        code.imul(a, a, 0xFF);
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_ge)], a);
    }
}

void A32EmitX64::EmitA32BXWritePC(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& arg = args[0];

    // Pseudocode:
    // if (new_pc & 1) {
    //    new_pc &= 0xFFFFFFFE;
    //    cpsr.T = true;
    // } else {
    //    new_pc &= 0xFFFFFFFC;
    //    cpsr.T = false;
    // }
    // We rely on the fact we disallow EFlag from changing within a block.

    if (arg.IsImmediate()) {
        u32 new_pc = arg.GetImmediateU32();
        u32 mask = Common::Bit<0>(new_pc) ? 0xFFFFFFFE : 0xFFFFFFFC;
        u32 et = 0;
        et |= ctx.Location().EFlag() ? 2 : 0;
        et |= Common::Bit<0>(new_pc) ? 1 : 0;

        code.mov(MJitStateReg(A32::Reg::PC), new_pc & mask);
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_et)], et);
    } else {
        if (ctx.Location().EFlag()) {
            Xbyak::Reg32 new_pc = ctx.reg_alloc.UseScratchGpr(arg).cvt32();
            Xbyak::Reg32 mask = ctx.reg_alloc.ScratchGpr().cvt32();
            Xbyak::Reg32 et = ctx.reg_alloc.ScratchGpr().cvt32();

            code.mov(mask, new_pc);
            code.and_(mask, 1);
            code.lea(et, ptr[mask.cvt64() + 2]);
            code.mov(dword[r15 + offsetof(A32JitState, CPSR_et)], et);
            code.lea(mask, ptr[mask.cvt64() + mask.cvt64() * 1 - 4]); // mask = pc & 1 ? 0xFFFFFFFE : 0xFFFFFFFC
            code.and_(new_pc, mask);
            code.mov(MJitStateReg(A32::Reg::PC), new_pc);
        } else {
            Xbyak::Reg32 new_pc = ctx.reg_alloc.UseScratchGpr(arg).cvt32();
            Xbyak::Reg32 mask = ctx.reg_alloc.ScratchGpr().cvt32();

            code.mov(mask, new_pc);
            code.and_(mask, 1);
            code.mov(dword[r15 + offsetof(A32JitState, CPSR_et)], mask);
            code.lea(mask, ptr[mask.cvt64() + mask.cvt64() * 1 - 4]); // mask = pc & 1 ? 0xFFFFFFFE : 0xFFFFFFFC
            code.and_(new_pc, mask);
            code.mov(MJitStateReg(A32::Reg::PC), new_pc);
        }
    }
}

void A32EmitX64::EmitA32CallSupervisor(A32EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(nullptr);

    code.SwitchMxcsrOnExit();
    code.mov(code.ABI_PARAM2, qword[r15 + offsetof(A32JitState, cycles_to_run)]);
    code.sub(code.ABI_PARAM2, qword[r15 + offsetof(A32JitState, cycles_remaining)]);
    Devirtualize<&A32::UserCallbacks::AddTicks>(config.callbacks).EmitCall(code);
    ctx.reg_alloc.EndOfAllocScope();
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, {}, args[0]);
    Devirtualize<&A32::UserCallbacks::CallSVC>(config.callbacks).EmitCall(code);
    Devirtualize<&A32::UserCallbacks::GetTicksRemaining>(config.callbacks).EmitCall(code);
    code.mov(qword[r15 + offsetof(A32JitState, cycles_to_run)], code.ABI_RETURN);
    code.mov(qword[r15 + offsetof(A32JitState, cycles_remaining)], code.ABI_RETURN);
    code.SwitchMxcsrOnEntry();
}

void A32EmitX64::EmitA32ExceptionRaised(A32EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(nullptr);
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate() && args[1].IsImmediate());
    u32 pc = args[0].GetImmediateU32();
    u64 exception = args[1].GetImmediateU64();
    Devirtualize<&A32::UserCallbacks::ExceptionRaised>(config.callbacks).EmitCall(code, [&](RegList param) {
        code.mov(param[0], pc);
        code.mov(param[1], exception);
    });
}

static u32 GetFpscrImpl(A32JitState* jit_state) {
    return jit_state->Fpscr();
}

void A32EmitX64::EmitA32GetFpscr(A32EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.HostCall(inst);
    code.mov(code.ABI_PARAM1, code.r15);

    code.stmxcsr(code.dword[code.r15 + offsetof(A32JitState, guest_MXCSR)]);
    code.CallFunction(&GetFpscrImpl);
}

static void SetFpscrImpl(u32 value, A32JitState* jit_state) {
    jit_state->SetFpscr(value);
}

void A32EmitX64::EmitA32SetFpscr(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(nullptr, args[0]);
    code.mov(code.ABI_PARAM2, code.r15);

    code.CallFunction(&SetFpscrImpl);
    code.ldmxcsr(code.dword[code.r15 + offsetof(A32JitState, guest_MXCSR)]);
}

void A32EmitX64::EmitA32GetFpscrNZCV(A32EmitContext& ctx, IR::Inst* inst) {
    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    code.mov(result, dword[r15 + offsetof(A32JitState, FPSCR_nzcv)]);
    ctx.reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetFpscrNZCV(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 value = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

    code.and_(value, 0b11000001'00000001);
    code.imul(value, value, 0b00010000'00100001);
    code.shl(value, 16);
    code.and_(value, 0xF0000000);

    code.mov(dword[r15 + offsetof(A32JitState, FPSCR_nzcv)], value);
}

void A32EmitX64::EmitA32ClearExclusive(A32EmitContext&, IR::Inst*) {
    code.mov(code.byte[r15 + offsetof(A32JitState, exclusive_state)], u8(0));
}

void A32EmitX64::EmitA32SetExclusive(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    Xbyak::Reg32 address = ctx.reg_alloc.UseGpr(args[0]).cvt32();

    code.mov(code.byte[r15 + offsetof(A32JitState, exclusive_state)], u8(1));
    code.mov(dword[r15 + offsetof(A32JitState, exclusive_address)], address);
}

template <typename T, T (A32::UserCallbacks::*raw_fn)(A32::VAddr)>
static void ReadMemory(BlockOfCode& code, RegAlloc& reg_alloc, IR::Inst* inst, const A32::UserConfig& config, const CodePtr wrapped_fn) {
    constexpr size_t bit_size = Common::BitSize<T>();
    auto args = reg_alloc.GetArgumentInfo(inst);

    if (!config.page_table) {
        reg_alloc.HostCall(inst, {}, args[0]);
        Devirtualize<raw_fn>(config.callbacks).EmitCall(code);
        return;
    }

    reg_alloc.UseScratch(args[0], ABI_PARAM2);

    Xbyak::Reg64 result = reg_alloc.ScratchGpr({ABI_RETURN});
    Xbyak::Reg32 vaddr = code.ABI_PARAM2.cvt32();
    Xbyak::Reg64 page_index = reg_alloc.ScratchGpr();
    Xbyak::Reg64 page_offset = reg_alloc.ScratchGpr();

    Xbyak::Label abort, end;

    code.mov(result, reinterpret_cast<u64>(config.page_table));
    code.mov(page_index.cvt32(), vaddr);
    code.shr(page_index.cvt32(), 12);
    code.mov(result, qword[result + page_index * 8]);
    code.test(result, result);
    code.jz(abort);
    code.mov(page_offset.cvt32(), vaddr);
    code.and_(page_offset.cvt32(), 4095);
    switch (bit_size) {
    case 8:
        code.movzx(result, code.byte[result + page_offset]);
        break;
    case 16:
        code.movzx(result, word[result + page_offset]);
        break;
    case 32:
        code.mov(result.cvt32(), dword[result + page_offset]);
        break;
    case 64:
        code.mov(result.cvt64(), qword[result + page_offset]);
        break;
    default:
        ASSERT_MSG(false, "Invalid bit_size");
        break;
    }
    code.jmp(end);
    code.L(abort);
    code.call(wrapped_fn);
    code.L(end);

    reg_alloc.DefineValue(inst, result);
}

template <typename T, void (A32::UserCallbacks::*raw_fn)(A32::VAddr, T)>
static void WriteMemory(BlockOfCode& code, RegAlloc& reg_alloc, IR::Inst* inst, const A32::UserConfig& config, const CodePtr wrapped_fn) {
    constexpr size_t bit_size = Common::BitSize<T>();
    auto args = reg_alloc.GetArgumentInfo(inst);

    if (!config.page_table) {
        reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
        Devirtualize<raw_fn>(config.callbacks).EmitCall(code);
        return;
    }

    reg_alloc.ScratchGpr({ABI_RETURN});
    reg_alloc.UseScratch(args[0], ABI_PARAM2);
    reg_alloc.UseScratch(args[1], ABI_PARAM3);

    Xbyak::Reg32 vaddr = code.ABI_PARAM2.cvt32();
    Xbyak::Reg64 value = code.ABI_PARAM3;
    Xbyak::Reg64 page_index = reg_alloc.ScratchGpr();
    Xbyak::Reg64 page_offset = reg_alloc.ScratchGpr();

    Xbyak::Label abort, end;

    code.mov(rax, reinterpret_cast<u64>(config.page_table));
    code.mov(page_index.cvt32(), vaddr);
    code.shr(page_index.cvt32(), 12);
    code.mov(rax, qword[rax + page_index * 8]);
    code.test(rax, rax);
    code.jz(abort);
    code.mov(page_offset.cvt32(), vaddr);
    code.and_(page_offset.cvt32(), 4095);
    switch (bit_size) {
    case 8:
        code.mov(code.byte[rax + page_offset], value.cvt8());
        break;
    case 16:
        code.mov(word[rax + page_offset], value.cvt16());
        break;
    case 32:
        code.mov(dword[rax + page_offset], value.cvt32());
        break;
    case 64:
        code.mov(qword[rax + page_offset], value.cvt64());
        break;
    default:
        ASSERT_MSG(false, "Invalid bit_size");
        break;
    }
    code.jmp(end);
    code.L(abort);
    code.call(wrapped_fn);
    code.L(end);
}

void A32EmitX64::EmitA32ReadMemory8(A32EmitContext& ctx, IR::Inst* inst) {
    ReadMemory<u8, &A32::UserCallbacks::MemoryRead8>(code, ctx.reg_alloc, inst, config, read_memory_8);
}

void A32EmitX64::EmitA32ReadMemory16(A32EmitContext& ctx, IR::Inst* inst) {
    ReadMemory<u16, &A32::UserCallbacks::MemoryRead16>(code, ctx.reg_alloc, inst, config, read_memory_16);
}

void A32EmitX64::EmitA32ReadMemory32(A32EmitContext& ctx, IR::Inst* inst) {
    ReadMemory<u32, &A32::UserCallbacks::MemoryRead32>(code, ctx.reg_alloc, inst, config, read_memory_32);
}

void A32EmitX64::EmitA32ReadMemory64(A32EmitContext& ctx, IR::Inst* inst) {
    ReadMemory<u64, &A32::UserCallbacks::MemoryRead64>(code, ctx.reg_alloc, inst, config, read_memory_64);
}

void A32EmitX64::EmitA32WriteMemory8(A32EmitContext& ctx, IR::Inst* inst) {
    WriteMemory<u8, &A32::UserCallbacks::MemoryWrite8>(code, ctx.reg_alloc, inst, config, write_memory_8);
}

void A32EmitX64::EmitA32WriteMemory16(A32EmitContext& ctx, IR::Inst* inst) {
    WriteMemory<u16, &A32::UserCallbacks::MemoryWrite16>(code, ctx.reg_alloc, inst, config, write_memory_16);
}

void A32EmitX64::EmitA32WriteMemory32(A32EmitContext& ctx, IR::Inst* inst) {
    WriteMemory<u32, &A32::UserCallbacks::MemoryWrite32>(code, ctx.reg_alloc, inst, config, write_memory_32);
}

void A32EmitX64::EmitA32WriteMemory64(A32EmitContext& ctx, IR::Inst* inst) {
    WriteMemory<u64, &A32::UserCallbacks::MemoryWrite64>(code, ctx.reg_alloc, inst, config, write_memory_64);
}

template <typename T, void (A32::UserCallbacks::*fn)(A32::VAddr, T)>
static void ExclusiveWrite(BlockOfCode& code, RegAlloc& reg_alloc, IR::Inst* inst, const A32::UserConfig& config, bool prepend_high_word) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (prepend_high_word) {
        reg_alloc.HostCall(nullptr, {}, args[0], args[1], args[2]);
    } else {
        reg_alloc.HostCall(nullptr, {}, args[0], args[1]);
    }
    Xbyak::Reg32 passed = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 tmp = code.ABI_RETURN.cvt32(); // Use one of the unusued HostCall registers.

    Xbyak::Label end;

    code.mov(passed, u32(1));
    code.cmp(code.byte[r15 + offsetof(A32JitState, exclusive_state)], u8(0));
    code.je(end);
    code.mov(tmp, code.ABI_PARAM2);
    code.xor_(tmp, dword[r15 + offsetof(A32JitState, exclusive_address)]);
    code.test(tmp, A32JitState::RESERVATION_GRANULE_MASK);
    code.jne(end);
    code.mov(code.byte[r15 + offsetof(A32JitState, exclusive_state)], u8(0));
    if (prepend_high_word) {
        code.mov(code.ABI_PARAM3.cvt32(), code.ABI_PARAM3.cvt32()); // zero extend to 64-bits
        code.shl(code.ABI_PARAM4, 32);
        code.or_(code.ABI_PARAM3, code.ABI_PARAM4);
    }
    Devirtualize<fn>(config.callbacks).EmitCall(code);
    code.xor_(passed, passed);
    code.L(end);

    reg_alloc.DefineValue(inst, passed);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory8(A32EmitContext& ctx, IR::Inst* inst) {
    ExclusiveWrite<u8, &A32::UserCallbacks::MemoryWrite8>(code, ctx.reg_alloc, inst, config, false);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory16(A32EmitContext& ctx, IR::Inst* inst) {
    ExclusiveWrite<u16, &A32::UserCallbacks::MemoryWrite16>(code, ctx.reg_alloc, inst, config, false);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory32(A32EmitContext& ctx, IR::Inst* inst) {
    ExclusiveWrite<u32, &A32::UserCallbacks::MemoryWrite32>(code, ctx.reg_alloc, inst, config, false);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory64(A32EmitContext& ctx, IR::Inst* inst) {
    ExclusiveWrite<u64, &A32::UserCallbacks::MemoryWrite64>(code, ctx.reg_alloc, inst, config, true);
}

static void EmitCoprocessorException() {
    ASSERT_MSG(false, "Should raise coproc exception here");
}

static void CallCoprocCallback(BlockOfCode& code, RegAlloc& reg_alloc, A32::Jit* jit_interface, A32::Coprocessor::Callback callback, IR::Inst* inst = nullptr, boost::optional<Argument&> arg0 = {}, boost::optional<Argument&> arg1 = {}) {
    reg_alloc.HostCall(inst, {}, {}, arg0, arg1);

    code.mov(code.ABI_PARAM1, reinterpret_cast<u64>(jit_interface));
    if (callback.user_arg) {
        code.mov(code.ABI_PARAM2, reinterpret_cast<u64>(*callback.user_arg));
    }

    code.CallFunction(callback.function);
}

void A32EmitX64::EmitA32CoprocInternalOperation(A32EmitContext& ctx, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    A32::CoprocReg CRn = static_cast<A32::CoprocReg>(coproc_info[4]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[5]);
    unsigned opc2 = static_cast<unsigned>(coproc_info[6]);

    std::shared_ptr<A32::Coprocessor> coproc = config.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileInternalOperation(two, opc1, CRd, CRn, CRm, opc2);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, ctx.reg_alloc, jit_interface, *action);
}

void A32EmitX64::EmitA32CoprocSendOneWord(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRn = static_cast<A32::CoprocReg>(coproc_info[3]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[4]);
    unsigned opc2 = static_cast<unsigned>(coproc_info[5]);

    std::shared_ptr<A32::Coprocessor> coproc = config.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileSendOneWord(two, opc1, CRn, CRm, opc2);
    switch (action.which()) {
    case 0:
        EmitCoprocessorException();
        return;
    case 1:
        CallCoprocCallback(code, ctx.reg_alloc, jit_interface, boost::get<A32::Coprocessor::Callback>(action), nullptr, args[1]);
        return;
    case 2: {
        u32* destination_ptr = boost::get<u32*>(action);

        Xbyak::Reg32 reg_word = ctx.reg_alloc.UseGpr(args[1]).cvt32();
        Xbyak::Reg64 reg_destination_addr = ctx.reg_alloc.ScratchGpr();

        code.mov(reg_destination_addr, reinterpret_cast<u64>(destination_ptr));
        code.mov(code.dword[reg_destination_addr], reg_word);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocSendTwoWords(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[3]);

    std::shared_ptr<A32::Coprocessor> coproc = config.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileSendTwoWords(two, opc, CRm);
    switch (action.which()) {
    case 0:
        EmitCoprocessorException();
        return;
    case 1:
        CallCoprocCallback(code, ctx.reg_alloc, jit_interface, boost::get<A32::Coprocessor::Callback>(action), nullptr, args[1], args[2]);
        return;
    case 2: {
        auto destination_ptrs = boost::get<std::array<u32*, 2>>(action);

        Xbyak::Reg32 reg_word1 = ctx.reg_alloc.UseGpr(args[1]).cvt32();
        Xbyak::Reg32 reg_word2 = ctx.reg_alloc.UseGpr(args[2]).cvt32();
        Xbyak::Reg64 reg_destination_addr = ctx.reg_alloc.ScratchGpr();

        code.mov(reg_destination_addr, reinterpret_cast<u64>(destination_ptrs[0]));
        code.mov(code.dword[reg_destination_addr], reg_word1);
        code.mov(reg_destination_addr, reinterpret_cast<u64>(destination_ptrs[1]));
        code.mov(code.dword[reg_destination_addr], reg_word2);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocGetOneWord(A32EmitContext& ctx, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRn = static_cast<A32::CoprocReg>(coproc_info[3]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[4]);
    unsigned opc2 = static_cast<unsigned>(coproc_info[5]);

    std::shared_ptr<A32::Coprocessor> coproc = config.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileGetOneWord(two, opc1, CRn, CRm, opc2);
    switch (action.which()) {
    case 0:
        EmitCoprocessorException();
        return;
    case 1:
        CallCoprocCallback(code, ctx.reg_alloc, jit_interface, boost::get<A32::Coprocessor::Callback>(action), inst);
        return;
    case 2: {
        u32* source_ptr = boost::get<u32*>(action);

        Xbyak::Reg32 reg_word = ctx.reg_alloc.ScratchGpr().cvt32();
        Xbyak::Reg64 reg_source_addr = ctx.reg_alloc.ScratchGpr();

        code.mov(reg_source_addr, reinterpret_cast<u64>(source_ptr));
        code.mov(reg_word, code.dword[reg_source_addr]);

        ctx.reg_alloc.DefineValue(inst, reg_word);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocGetTwoWords(A32EmitContext& ctx, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc = coproc_info[2];
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[3]);

    std::shared_ptr<A32::Coprocessor> coproc = config.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileGetTwoWords(two, opc, CRm);
    switch (action.which()) {
    case 0:
        EmitCoprocessorException();
        return;
    case 1:
        CallCoprocCallback(code, ctx.reg_alloc, jit_interface, boost::get<A32::Coprocessor::Callback>(action), inst);
        return;
    case 2: {
        auto source_ptrs = boost::get<std::array<u32*, 2>>(action);

        Xbyak::Reg64 reg_result = ctx.reg_alloc.ScratchGpr();
        Xbyak::Reg64 reg_destination_addr = ctx.reg_alloc.ScratchGpr();
        Xbyak::Reg64 reg_tmp = ctx.reg_alloc.ScratchGpr();

        code.mov(reg_destination_addr, reinterpret_cast<u64>(source_ptrs[1]));
        code.mov(reg_result.cvt32(), code.dword[reg_destination_addr]);
        code.shl(reg_result, 32);
        code.mov(reg_destination_addr, reinterpret_cast<u64>(source_ptrs[0]));
        code.mov(reg_tmp.cvt32(), code.dword[reg_destination_addr]);
        code.or_(reg_result, reg_tmp);

        ctx.reg_alloc.DefineValue(inst, reg_result);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocLoadWords(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    bool long_transfer = coproc_info[2] != 0;
    A32::CoprocReg CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    bool has_option = coproc_info[4] != 0;
    boost::optional<u8> option{has_option, coproc_info[5]};

    std::shared_ptr<A32::Coprocessor> coproc = config.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileLoadWords(two, long_transfer, CRd, option);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, ctx.reg_alloc, jit_interface, *action, nullptr, args[1]);
}

void A32EmitX64::EmitA32CoprocStoreWords(A32EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    bool long_transfer = coproc_info[2] != 0;
    A32::CoprocReg CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    bool has_option = coproc_info[4] != 0;
    boost::optional<u8> option{has_option, coproc_info[5]};

    std::shared_ptr<A32::Coprocessor> coproc = config.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileStoreWords(two, long_transfer, CRd, option);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, ctx.reg_alloc, jit_interface, *action, nullptr, args[1]);
}

void A32EmitX64::EmitTerminalImpl(IR::Term::Interpret terminal, IR::LocationDescriptor initial_location) {
    ASSERT_MSG(A32::LocationDescriptor{terminal.next}.TFlag() == A32::LocationDescriptor{initial_location}.TFlag(), "Unimplemented");
    ASSERT_MSG(A32::LocationDescriptor{terminal.next}.EFlag() == A32::LocationDescriptor{initial_location}.EFlag(), "Unimplemented");
    ASSERT_MSG(terminal.num_instructions == 1, "Unimplemented");

    code.mov(code.ABI_PARAM2.cvt32(), A32::LocationDescriptor{terminal.next}.PC());
    code.mov(code.ABI_PARAM3.cvt32(), 1);
    code.mov(MJitStateReg(A32::Reg::PC), code.ABI_PARAM2.cvt32());
    code.SwitchMxcsrOnExit();
    Devirtualize<&A32::UserCallbacks::InterpreterFallback>(config.callbacks).EmitCall(code);
    code.ReturnFromRunCode(true); // TODO: Check cycles
}

void A32EmitX64::EmitTerminalImpl(IR::Term::ReturnToDispatch, IR::LocationDescriptor) {
    code.ReturnFromRunCode();
}

static u32 CalculateCpsr_et(const IR::LocationDescriptor& arg) {
    const A32::LocationDescriptor desc{arg};
    u32 et = 0;
    et |= desc.EFlag() ? 2 : 0;
    et |= desc.TFlag() ? 1 : 0;
    return et;
}

void A32EmitX64::EmitTerminalImpl(IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location) {
    if (CalculateCpsr_et(terminal.next) != CalculateCpsr_et(initial_location)) {
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_et)], CalculateCpsr_et(terminal.next));
    }

    code.cmp(qword[r15 + offsetof(A32JitState, cycles_remaining)], 0);

    patch_information[terminal.next].jg.emplace_back(code.getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJg(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJg(terminal.next);
    }
    Xbyak::Label dest;
    code.jmp(dest, Xbyak::CodeGenerator::T_NEAR);

    code.SwitchToFarCode();
    code.align(16);
    code.L(dest);
    code.mov(MJitStateReg(A32::Reg::PC), A32::LocationDescriptor{terminal.next}.PC());
    PushRSBHelper(rax, rbx, terminal.next);
    code.ForceReturnFromRunCode();
    code.SwitchToNearCode();
}

void A32EmitX64::EmitTerminalImpl(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location) {
    if (CalculateCpsr_et(terminal.next) != CalculateCpsr_et(initial_location)) {
        code.mov(dword[r15 + offsetof(A32JitState, CPSR_et)], CalculateCpsr_et(terminal.next));
    }

    patch_information[terminal.next].jmp.emplace_back(code.getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJmp(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJmp(terminal.next);
    }
}

void A32EmitX64::EmitTerminalImpl(IR::Term::PopRSBHint, IR::LocationDescriptor) {
    // This calculation has to match up with IREmitter::PushRSB
    // TODO: Optimization is available here based on known state of FPSCR_mode and CPSR_et.
    code.mov(ecx, MJitStateReg(A32::Reg::PC));
    code.shl(rcx, 32);
    code.mov(ebx, dword[r15 + offsetof(A32JitState, FPSCR_mode)]);
    code.or_(ebx, dword[r15 + offsetof(A32JitState, CPSR_et)]);
    code.or_(rbx, rcx);

    code.mov(eax, dword[r15 + offsetof(A32JitState, rsb_ptr)]);
    code.sub(eax, 1);
    code.and_(eax, u32(A32JitState::RSBPtrMask));
    code.mov(dword[r15 + offsetof(A32JitState, rsb_ptr)], eax);
    code.cmp(rbx, qword[r15 + offsetof(A32JitState, rsb_location_descriptors) + rax * sizeof(u64)]);
    code.jne(code.GetReturnFromRunCodeAddress());
    code.mov(rax, qword[r15 + offsetof(A32JitState, rsb_codeptrs) + rax * sizeof(u64)]);
    code.jmp(rax);
}

void A32EmitX64::EmitTerminalImpl(IR::Term::If terminal, IR::LocationDescriptor initial_location) {
    Xbyak::Label pass = EmitCond(terminal.if_);
    EmitTerminal(terminal.else_, initial_location);
    code.L(pass);
    EmitTerminal(terminal.then_, initial_location);
}

void A32EmitX64::EmitTerminalImpl(IR::Term::CheckBit, IR::LocationDescriptor) {
    ASSERT_MSG(false, "Term::CheckBit should never be emitted by the A32 frontend");
}

void A32EmitX64::EmitTerminalImpl(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location) {
    code.cmp(code.byte[r15 + offsetof(A32JitState, halt_requested)], u8(0));
    code.jne(code.GetForceReturnFromRunCodeAddress());
    EmitTerminal(terminal.else_, initial_location);
}

void A32EmitX64::EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code.getCurr();
    if (target_code_ptr) {
        code.jg(target_code_ptr);
    } else {
        code.mov(MJitStateReg(A32::Reg::PC), A32::LocationDescriptor{target_desc}.PC());
        code.jg(code.GetReturnFromRunCodeAddress());
    }
    code.EnsurePatchLocationSize(patch_location, 14);
}

void A32EmitX64::EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code.getCurr();
    if (target_code_ptr) {
        code.jmp(target_code_ptr);
    } else {
        code.mov(MJitStateReg(A32::Reg::PC), A32::LocationDescriptor{target_desc}.PC());
        code.jmp(code.GetReturnFromRunCodeAddress());
    }
    code.EnsurePatchLocationSize(patch_location, 13);
}

void A32EmitX64::EmitPatchMovRcx(CodePtr target_code_ptr) {
    if (!target_code_ptr) {
        target_code_ptr = code.GetReturnFromRunCodeAddress();
    }
    const CodePtr patch_location = code.getCurr();
    code.mov(code.rcx, reinterpret_cast<u64>(target_code_ptr));
    code.EnsurePatchLocationSize(patch_location, 10);
}

} // namespace Dynarmic::BackendX64
