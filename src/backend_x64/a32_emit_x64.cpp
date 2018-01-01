/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <unordered_map>
#include <unordered_set>

#include <dynarmic/coprocessor.h>

#include "backend_x64/a32_emit_x64.h"
#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "backend_x64/jitstate.h"
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

namespace Dynarmic {
namespace BackendX64 {

using namespace Xbyak::util;

static Xbyak::Address MJitStateReg(A32::Reg reg) {
    return dword[r15 + offsetof(JitState, Reg) + sizeof(u32) * static_cast<size_t>(reg)];
}

static Xbyak::Address MJitStateExtReg(A32::ExtReg reg) {
    if (A32::IsSingleExtReg(reg)) {
        size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::S0);
        return dword[r15 + offsetof(JitState, ExtReg) + sizeof(u32) * index];
    }
    if (A32::IsDoubleExtReg(reg)) {
        size_t index = static_cast<size_t>(reg) - static_cast<size_t>(A32::ExtReg::D0);
        return qword[r15 + offsetof(JitState, ExtReg) + sizeof(u64) * index];
    }
    ASSERT_MSG(false, "Should never happen.");
}

A32EmitX64::A32EmitX64(BlockOfCode* code, UserCallbacks cb, Jit* jit_interface)
    : EmitX64(code, cb, jit_interface) {}

A32EmitX64::~A32EmitX64() {}

A32EmitX64::BlockDescriptor A32EmitX64::Emit(IR::Block& block) {
    code->align();
    const u8* const entrypoint = code->getCurr();

    // Start emitting.
    EmitCondPrelude(block);

    RegAlloc reg_alloc{code};

    for (auto iter = block.begin(); iter != block.end(); ++iter) {
        IR::Inst* inst = &*iter;

        // Call the relevant Emit* member function.
        switch (inst->GetOpcode()) {

#define OPCODE(name, type, ...)                              \
        case IR::Opcode::name:                               \
            A32EmitX64::Emit##name(reg_alloc, block, inst);  \
            break;
#define A32OPC(name, type, ...)                                 \
        case IR::Opcode::A32##name:                             \
            A32EmitX64::EmitA32##name(reg_alloc, block, inst);  \
            break;
#include "frontend/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC

        default:
            ASSERT_MSG(false, "Invalid opcode %zu", static_cast<size_t>(inst->GetOpcode()));
            break;
        }

        reg_alloc.EndOfAllocScope();
    }

    reg_alloc.AssertNoMoreUses();

    EmitAddCycles(block.CycleCount());
    EmitX64::EmitTerminal(block.GetTerminal(), block.Location());
    code->int3();

    const A32::LocationDescriptor descriptor = block.Location();
    Patch(descriptor, entrypoint);

    const size_t size = static_cast<size_t>(code->getCurr() - entrypoint);
    const A32::LocationDescriptor end_location = block.EndLocation();
    const auto range = boost::icl::discrete_interval<u32>::closed(descriptor.PC(), end_location.PC() - 1);
    A32EmitX64::BlockDescriptor block_desc{entrypoint, size, block.Location(), range};
    block_descriptors.emplace(descriptor.UniqueHash(), block_desc);
    block_ranges.add(std::make_pair(range, std::set<IR::LocationDescriptor>{descriptor}));

    return block_desc;
}

void A32EmitX64::EmitA32GetRegister(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    A32::Reg reg = inst->GetArg(0).GetA32RegRef();

    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, MJitStateReg(reg));
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32GetExtendedRegister32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsSingleExtReg(reg));

    Xbyak::Xmm result = reg_alloc.ScratchXmm();
    code->movss(result, MJitStateExtReg(reg));
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32GetExtendedRegister64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg));

    Xbyak::Xmm result = reg_alloc.ScratchXmm();
    code->movsd(result, MJitStateExtReg(reg));
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetRegister(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    A32::Reg reg = inst->GetArg(0).GetA32RegRef();
    if (args[1].IsImmediate()) {
        code->mov(MJitStateReg(reg), args[1].GetImmediateU32());
    } else if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = reg_alloc.UseXmm(args[1]);
        code->movd(MJitStateReg(reg), to_store);
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseGpr(args[1]).cvt32();
        code->mov(MJitStateReg(reg), to_store);
    }
}

void A32EmitX64::EmitA32SetExtendedRegister32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsSingleExtReg(reg));
    if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = reg_alloc.UseXmm(args[1]);
        code->movss(MJitStateExtReg(reg), to_store);
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseGpr(args[1]).cvt32();
        code->mov(MJitStateExtReg(reg), to_store);
    }
}

void A32EmitX64::EmitA32SetExtendedRegister64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
    ASSERT(A32::IsDoubleExtReg(reg));
    if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = reg_alloc.UseXmm(args[1]);
        code->movsd(MJitStateExtReg(reg), to_store);
    } else {
        Xbyak::Reg64 to_store = reg_alloc.UseGpr(args[1]);
        code->mov(MJitStateExtReg(reg), to_store);
    }
}

static u32 GetCpsrImpl(JitState* jit_state) {
    return jit_state->Cpsr();
}

void A32EmitX64::EmitA32GetCpsr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    if (code->DoesCpuSupport(Xbyak::util::Cpu::tBMI2)) {
        Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
        Xbyak::Reg32 b = reg_alloc.ScratchGpr().cvt32();
        Xbyak::Reg32 c = reg_alloc.ScratchGpr().cvt32();

        code->mov(c, dword[r15 + offsetof(JitState, CPSR_ge)]);
        // Here we observe that CPSR_q and CPSR_nzcv are right next to each other in memory,
        // so we load them both at the same time with one 64-bit read. This allows us to
        // extract all of their bits together at once with one pext.
        code->mov(result.cvt64(), qword[r15 + offsetof(JitState, CPSR_q)]);
        code->mov(b.cvt64(), 0xF000000000000001ull);
        code->pext(result.cvt64(), result.cvt64(), b.cvt64());
        code->mov(b, 0x80808080);
        code->pext(c.cvt64(), c.cvt64(), b.cvt64());
        code->shl(result, 27);
        code->shl(c, 16);
        code->or_(result, c);
        code->mov(b, 0x00000220);
        code->mov(c, dword[r15 + offsetof(JitState, CPSR_et)]);
        code->pdep(c.cvt64(), c.cvt64(), b.cvt64());
        code->or_(result, dword[r15 + offsetof(JitState, CPSR_jaifm)]);
        code->or_(result, c);

        reg_alloc.DefineValue(inst, result);
    } else {
        reg_alloc.HostCall(inst);
        code->mov(code->ABI_PARAM1, code->r15);
        code->CallFunction(&GetCpsrImpl);
    }
}

static void SetCpsrImpl(u32 value, JitState* jit_state) {
    jit_state->SetCpsr(value);
}

void A32EmitX64::EmitA32SetCpsr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.HostCall(nullptr, args[0]);
    code->mov(code->ABI_PARAM2, code->r15);
    code->CallFunction(&SetCpsrImpl);
}

void A32EmitX64::EmitA32SetCpsrNZCV(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        u32 imm = args[0].GetImmediateU32();

        code->mov(dword[r15 + offsetof(JitState, CPSR_nzcv)], u32(imm & 0xF0000000));
    } else {
        Xbyak::Reg32 a = reg_alloc.UseScratchGpr(args[0]).cvt32();

        code->and_(a, 0xF0000000);
        code->mov(dword[r15 + offsetof(JitState, CPSR_nzcv)], a);
    }
}

void A32EmitX64::EmitA32SetCpsrNZCVQ(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        u32 imm = args[0].GetImmediateU32();

        code->mov(dword[r15 + offsetof(JitState, CPSR_nzcv)], u32(imm & 0xF0000000));
        code->mov(code->byte[r15 + offsetof(JitState, CPSR_q)], u8((imm & 0x08000000) != 0 ? 1 : 0));
    } else {
        Xbyak::Reg32 a = reg_alloc.UseScratchGpr(args[0]).cvt32();

        code->bt(a, 27);
        code->setc(code->byte[r15 + offsetof(JitState, CPSR_q)]);
        code->and_(a, 0xF0000000);
        code->mov(dword[r15 + offsetof(JitState, CPSR_nzcv)], a);
    }
}

void A32EmitX64::EmitA32GetNFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 31);
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetNFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    constexpr size_t flag_bit = 31;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], flag_mask);
        } else {
            code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseScratchGpr(args[0]).cvt32();

        code->shl(to_store, flag_bit);
        code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32GetZFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 30);
    code->and_(result, 1);
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetZFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    constexpr size_t flag_bit = 30;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], flag_mask);
        } else {
            code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseScratchGpr(args[0]).cvt32();

        code->shl(to_store, flag_bit);
        code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32GetCFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 29);
    code->and_(result, 1);
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetCFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    constexpr size_t flag_bit = 29;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], flag_mask);
        } else {
            code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseScratchGpr(args[0]).cvt32();

        code->shl(to_store, flag_bit);
        code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32GetVFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 28);
    code->and_(result, 1);
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetVFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    constexpr size_t flag_bit = 28;
    constexpr u32 flag_mask = 1u << flag_bit;
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1()) {
            code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], flag_mask);
        } else {
            code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        }
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseScratchGpr(args[0]).cvt32();

        code->shl(to_store, flag_bit);
        code->and_(dword[r15 + offsetof(JitState, CPSR_nzcv)], ~flag_mask);
        code->or_(dword[r15 + offsetof(JitState, CPSR_nzcv)], to_store);
    }
}

void A32EmitX64::EmitA32OrQFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1())
            code->mov(dword[r15 + offsetof(JitState, CPSR_q)], 1);
    } else {
        Xbyak::Reg8 to_store = reg_alloc.UseGpr(args[0]).cvt8();

        code->or_(code->byte[r15 + offsetof(JitState, CPSR_q)], to_store);
    }
}

void A32EmitX64::EmitA32GetGEFlags(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Xmm result = reg_alloc.ScratchXmm();
    code->movd(result, dword[r15 + offsetof(JitState, CPSR_ge)]);
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetGEFlags(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    ASSERT(!args[0].IsImmediate());

    if (args[0].IsInXmm()) {
        Xbyak::Xmm to_store = reg_alloc.UseXmm(args[0]);
        code->movd(dword[r15 + offsetof(JitState, CPSR_ge)], to_store);
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseGpr(args[0]).cvt32();
        code->mov(dword[r15 + offsetof(JitState, CPSR_ge)], to_store);
    }
}

void A32EmitX64::EmitA32SetGEFlagsCompressed(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        u32 imm = args[0].GetImmediateU32();
        u32 ge = 0;
        ge |= Common::Bit<19>(imm) ? 0xFF000000 : 0;
        ge |= Common::Bit<18>(imm) ? 0x00FF0000 : 0;
        ge |= Common::Bit<17>(imm) ? 0x0000FF00 : 0;
        ge |= Common::Bit<16>(imm) ? 0x000000FF : 0;

        code->mov(dword[r15 + offsetof(JitState, CPSR_ge)], ge);
    } else if (code->DoesCpuSupport(Xbyak::util::Cpu::tBMI2)) {
        Xbyak::Reg32 a = reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 b = reg_alloc.ScratchGpr().cvt32();

        code->mov(b, 0x01010101);
        code->shr(a, 16);
        code->pdep(a, a, b);
        code->imul(a, a, 0xFF);
        code->mov(dword[r15 + offsetof(JitState, CPSR_ge)], a);
    } else {
        Xbyak::Reg32 a = reg_alloc.UseScratchGpr(args[0]).cvt32();

        code->shr(a, 16);
        code->and_(a, 0xF);
        code->imul(a, a, 0x00204081);
        code->and_(a, 0x01010101);
        code->imul(a, a, 0xFF);
        code->mov(dword[r15 + offsetof(JitState, CPSR_ge)], a);
    }
}

void A32EmitX64::EmitA32BXWritePC(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
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
        et |= A32::LocationDescriptor{block.Location()}.EFlag() ? 2 : 0;
        et |= Common::Bit<0>(new_pc) ? 1 : 0;

        code->mov(MJitStateReg(A32::Reg::PC), new_pc & mask);
        code->mov(dword[r15 + offsetof(JitState, CPSR_et)], et);
    } else {
        if (A32::LocationDescriptor{block.Location()}.EFlag()) {
            Xbyak::Reg32 new_pc = reg_alloc.UseScratchGpr(arg).cvt32();
            Xbyak::Reg32 mask = reg_alloc.ScratchGpr().cvt32();
            Xbyak::Reg32 et = reg_alloc.ScratchGpr().cvt32();

            code->mov(mask, new_pc);
            code->and_(mask, 1);
            code->lea(et, ptr[mask.cvt64() + 2]);
            code->mov(dword[r15 + offsetof(JitState, CPSR_et)], et);
            code->lea(mask, ptr[mask.cvt64() + mask.cvt64() * 1 - 4]); // mask = pc & 1 ? 0xFFFFFFFE : 0xFFFFFFFC
            code->and_(new_pc, mask);
            code->mov(MJitStateReg(A32::Reg::PC), new_pc);
        } else {
            Xbyak::Reg32 new_pc = reg_alloc.UseScratchGpr(arg).cvt32();
            Xbyak::Reg32 mask = reg_alloc.ScratchGpr().cvt32();

            code->mov(mask, new_pc);
            code->and_(mask, 1);
            code->mov(dword[r15 + offsetof(JitState, CPSR_et)], mask);
            code->lea(mask, ptr[mask.cvt64() + mask.cvt64() * 1 - 4]); // mask = pc & 1 ? 0xFFFFFFFE : 0xFFFFFFFC
            code->and_(new_pc, mask);
            code->mov(MJitStateReg(A32::Reg::PC), new_pc);
        }
    }
}

void A32EmitX64::EmitA32CallSupervisor(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    reg_alloc.HostCall(nullptr);

    code->SwitchMxcsrOnExit();
    code->mov(code->ABI_PARAM1, qword[r15 + offsetof(JitState, cycles_to_run)]);
    code->sub(code->ABI_PARAM1, qword[r15 + offsetof(JitState, cycles_remaining)]);
    code->CallFunction(cb.AddTicks);
    reg_alloc.EndOfAllocScope();
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.HostCall(nullptr, args[0]);
    code->CallFunction(cb.CallSVC);
    code->CallFunction(cb.GetTicksRemaining);
    code->mov(qword[r15 + offsetof(JitState, cycles_to_run)], code->ABI_RETURN);
    code->mov(qword[r15 + offsetof(JitState, cycles_remaining)], code->ABI_RETURN);
    code->SwitchMxcsrOnEntry();
}

static u32 GetFpscrImpl(JitState* jit_state) {
    return jit_state->Fpscr();
}

void A32EmitX64::EmitA32GetFpscr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    reg_alloc.HostCall(inst);
    code->mov(code->ABI_PARAM1, code->r15);

    code->stmxcsr(code->dword[code->r15 + offsetof(JitState, guest_MXCSR)]);
    code->CallFunction(&GetFpscrImpl);
}

static void SetFpscrImpl(u32 value, JitState* jit_state) {
    jit_state->SetFpscr(value);
}

void A32EmitX64::EmitA32SetFpscr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.HostCall(nullptr, args[0]);
    code->mov(code->ABI_PARAM2, code->r15);

    code->CallFunction(&SetFpscrImpl);
    code->ldmxcsr(code->dword[code->r15 + offsetof(JitState, guest_MXCSR)]);
}

void A32EmitX64::EmitA32GetFpscrNZCV(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, FPSCR_nzcv)]);
    reg_alloc.DefineValue(inst, result);
}

void A32EmitX64::EmitA32SetFpscrNZCV(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 value = reg_alloc.UseGpr(args[0]).cvt32();

    code->mov(dword[r15 + offsetof(JitState, FPSCR_nzcv)], value);
}

void A32EmitX64::EmitA32ClearExclusive(RegAlloc&, IR::Block&, IR::Inst*) {
    code->mov(code->byte[r15 + offsetof(JitState, exclusive_state)], u8(0));
}

void A32EmitX64::EmitA32SetExclusive(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[1].IsImmediate());
    Xbyak::Reg32 address = reg_alloc.UseGpr(args[0]).cvt32();

    code->mov(code->byte[r15 + offsetof(JitState, exclusive_state)], u8(1));
    code->mov(dword[r15 + offsetof(JitState, exclusive_address)], address);
}

template <typename FunctionPointer>
static void ReadMemory(BlockOfCode* code, RegAlloc& reg_alloc, IR::Inst* inst, UserCallbacks& cb, size_t bit_size, FunctionPointer fn) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    if (!cb.page_table) {
        reg_alloc.HostCall(inst, args[0]);
        code->CallFunction(fn);
        return;
    }

    reg_alloc.UseScratch(args[0], ABI_PARAM1);

    Xbyak::Reg64 result = reg_alloc.ScratchGpr({ABI_RETURN});
    Xbyak::Reg32 vaddr = code->ABI_PARAM1.cvt32();
    Xbyak::Reg64 page_index = reg_alloc.ScratchGpr();
    Xbyak::Reg64 page_offset = reg_alloc.ScratchGpr();

    Xbyak::Label abort, end;

    code->mov(result, reinterpret_cast<u64>(cb.page_table));
    code->mov(page_index.cvt32(), vaddr);
    code->shr(page_index.cvt32(), 12);
    code->mov(result, qword[result + page_index * 8]);
    code->test(result, result);
    code->jz(abort);
    code->mov(page_offset.cvt32(), vaddr);
    code->and_(page_offset.cvt32(), 4095);
    switch (bit_size) {
    case 8:
        code->movzx(result, code->byte[result + page_offset]);
        break;
    case 16:
        code->movzx(result, word[result + page_offset]);
        break;
    case 32:
        code->mov(result.cvt32(), dword[result + page_offset]);
        break;
    case 64:
        code->mov(result.cvt64(), qword[result + page_offset]);
        break;
    default:
        ASSERT_MSG(false, "Invalid bit_size");
        break;
    }
    code->jmp(end);
    code->L(abort);
    code->call(code->GetMemoryReadCallback(bit_size));
    code->L(end);

    reg_alloc.DefineValue(inst, result);
}

template<typename FunctionPointer>
static void WriteMemory(BlockOfCode* code, RegAlloc& reg_alloc, IR::Inst* inst, UserCallbacks& cb, size_t bit_size, FunctionPointer fn) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    if (!cb.page_table) {
        reg_alloc.HostCall(nullptr, args[0], args[1]);
        code->CallFunction(fn);
        return;
    }

    reg_alloc.ScratchGpr({ABI_RETURN});
    reg_alloc.UseScratch(args[0], ABI_PARAM1);
    reg_alloc.UseScratch(args[1], ABI_PARAM2);

    Xbyak::Reg32 vaddr = code->ABI_PARAM1.cvt32();
    Xbyak::Reg64 value = code->ABI_PARAM2;
    Xbyak::Reg64 page_index = reg_alloc.ScratchGpr();
    Xbyak::Reg64 page_offset = reg_alloc.ScratchGpr();

    Xbyak::Label abort, end;

    code->mov(rax, reinterpret_cast<u64>(cb.page_table));
    code->mov(page_index.cvt32(), vaddr);
    code->shr(page_index.cvt32(), 12);
    code->mov(rax, qword[rax + page_index * 8]);
    code->test(rax, rax);
    code->jz(abort);
    code->mov(page_offset.cvt32(), vaddr);
    code->and_(page_offset.cvt32(), 4095);
    switch (bit_size) {
    case 8:
        code->mov(code->byte[rax + page_offset], value.cvt8());
        break;
    case 16:
        code->mov(word[rax + page_offset], value.cvt16());
        break;
    case 32:
        code->mov(dword[rax + page_offset], value.cvt32());
        break;
    case 64:
        code->mov(qword[rax + page_offset], value.cvt64());
        break;
    default:
        ASSERT_MSG(false, "Invalid bit_size");
        break;
    }
    code->jmp(end);
    code->L(abort);
    code->call(code->GetMemoryWriteCallback(bit_size));
    code->L(end);
}

void A32EmitX64::EmitA32ReadMemory8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 8, cb.memory.Read8);
}

void A32EmitX64::EmitA32ReadMemory16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 16, cb.memory.Read16);
}

void A32EmitX64::EmitA32ReadMemory32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 32, cb.memory.Read32);
}

void A32EmitX64::EmitA32ReadMemory64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 64, cb.memory.Read64);
}

void A32EmitX64::EmitA32WriteMemory8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    WriteMemory(code, reg_alloc, inst, cb, 8, cb.memory.Write8);
}

void A32EmitX64::EmitA32WriteMemory16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    WriteMemory(code, reg_alloc, inst, cb, 16, cb.memory.Write16);
}

void A32EmitX64::EmitA32WriteMemory32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    WriteMemory(code, reg_alloc, inst, cb, 32, cb.memory.Write32);
}

void A32EmitX64::EmitA32WriteMemory64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    WriteMemory(code, reg_alloc, inst, cb, 64, cb.memory.Write64);
}

template <typename FunctionPointer>
static void ExclusiveWrite(BlockOfCode* code, RegAlloc& reg_alloc, IR::Inst* inst, FunctionPointer fn, bool prepend_high_word) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (prepend_high_word) {
        reg_alloc.HostCall(nullptr, args[0], args[1], args[2]);
    } else {
        reg_alloc.HostCall(nullptr, args[0], args[1]);
    }
    Xbyak::Reg32 passed = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 tmp = code->ABI_RETURN.cvt32(); // Use one of the unusued HostCall registers.

    Xbyak::Label end;

    code->mov(passed, u32(1));
    code->cmp(code->byte[r15 + offsetof(JitState, exclusive_state)], u8(0));
    code->je(end);
    code->mov(tmp, code->ABI_PARAM1);
    code->xor_(tmp, dword[r15 + offsetof(JitState, exclusive_address)]);
    code->test(tmp, JitState::RESERVATION_GRANULE_MASK);
    code->jne(end);
    code->mov(code->byte[r15 + offsetof(JitState, exclusive_state)], u8(0));
    if (prepend_high_word) {
        code->mov(code->ABI_PARAM2.cvt32(), code->ABI_PARAM2.cvt32()); // zero extend to 64-bits
        code->shl(code->ABI_PARAM3, 32);
        code->or_(code->ABI_PARAM2, code->ABI_PARAM3);
    }
    code->CallFunction(fn);
    code->xor_(passed, passed);
    code->L(end);

    reg_alloc.DefineValue(inst, passed);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ExclusiveWrite(code, reg_alloc, inst, cb.memory.Write8, false);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ExclusiveWrite(code, reg_alloc, inst, cb.memory.Write16, false);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ExclusiveWrite(code, reg_alloc, inst, cb.memory.Write32, false);
}

void A32EmitX64::EmitA32ExclusiveWriteMemory64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ExclusiveWrite(code, reg_alloc, inst, cb.memory.Write64, true);
}

static void EmitCoprocessorException() {
    ASSERT_MSG(false, "Should raise coproc exception here");
}

static void CallCoprocCallback(BlockOfCode* code, RegAlloc& reg_alloc, Jit* jit_interface, Coprocessor::Callback callback, IR::Inst* inst = nullptr, boost::optional<Argument&> arg0 = {}, boost::optional<Argument&> arg1 = {}) {
    reg_alloc.HostCall(inst, {}, {}, arg0, arg1);

    code->mov(code->ABI_PARAM1, reinterpret_cast<u64>(jit_interface));
    if (callback.user_arg) {
        code->mov(code->ABI_PARAM2, reinterpret_cast<u64>(*callback.user_arg));
    }

    code->CallFunction(callback.function);
}

void A32EmitX64::EmitA32CoprocInternalOperation(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    A32::CoprocReg CRn = static_cast<A32::CoprocReg>(coproc_info[4]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[5]);
    unsigned opc2 = static_cast<unsigned>(coproc_info[6]);

    std::shared_ptr<Coprocessor> coproc = cb.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileInternalOperation(two, opc1, CRd, CRn, CRm, opc2);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, reg_alloc, jit_interface, *action);
}

void A32EmitX64::EmitA32CoprocSendOneWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRn = static_cast<A32::CoprocReg>(coproc_info[3]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[4]);
    unsigned opc2 = static_cast<unsigned>(coproc_info[5]);

    std::shared_ptr<Coprocessor> coproc = cb.coprocessors[coproc_num];
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
        CallCoprocCallback(code, reg_alloc, jit_interface, boost::get<Coprocessor::Callback>(action), nullptr, args[1]);
        return;
    case 2: {
        u32* destination_ptr = boost::get<u32*>(action);

        Xbyak::Reg32 reg_word = reg_alloc.UseGpr(args[1]).cvt32();
        Xbyak::Reg64 reg_destination_addr = reg_alloc.ScratchGpr();

        code->mov(reg_destination_addr, reinterpret_cast<u64>(destination_ptr));
        code->mov(code->dword[reg_destination_addr], reg_word);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocSendTwoWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[3]);

    std::shared_ptr<Coprocessor> coproc = cb.coprocessors[coproc_num];
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
        CallCoprocCallback(code, reg_alloc, jit_interface, boost::get<Coprocessor::Callback>(action), nullptr, args[1], args[2]);
        return;
    case 2: {
        auto destination_ptrs = boost::get<std::array<u32*, 2>>(action);

        Xbyak::Reg32 reg_word1 = reg_alloc.UseGpr(args[1]).cvt32();
        Xbyak::Reg32 reg_word2 = reg_alloc.UseGpr(args[2]).cvt32();
        Xbyak::Reg64 reg_destination_addr = reg_alloc.ScratchGpr();

        code->mov(reg_destination_addr, reinterpret_cast<u64>(destination_ptrs[0]));
        code->mov(code->dword[reg_destination_addr], reg_word1);
        code->mov(reg_destination_addr, reinterpret_cast<u64>(destination_ptrs[1]));
        code->mov(code->dword[reg_destination_addr], reg_word2);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocGetOneWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    A32::CoprocReg CRn = static_cast<A32::CoprocReg>(coproc_info[3]);
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[4]);
    unsigned opc2 = static_cast<unsigned>(coproc_info[5]);

    std::shared_ptr<Coprocessor> coproc = cb.coprocessors[coproc_num];
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
        CallCoprocCallback(code, reg_alloc, jit_interface, boost::get<Coprocessor::Callback>(action), inst);
        return;
    case 2: {
        u32* source_ptr = boost::get<u32*>(action);

        Xbyak::Reg32 reg_word = reg_alloc.ScratchGpr().cvt32();
        Xbyak::Reg64 reg_source_addr = reg_alloc.ScratchGpr();

        code->mov(reg_source_addr, reinterpret_cast<u64>(source_ptr));
        code->mov(reg_word, code->dword[reg_source_addr]);

        reg_alloc.DefineValue(inst, reg_word);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocGetTwoWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc = coproc_info[2];
    A32::CoprocReg CRm = static_cast<A32::CoprocReg>(coproc_info[3]);

    std::shared_ptr<Coprocessor> coproc = cb.coprocessors[coproc_num];
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
        CallCoprocCallback(code, reg_alloc, jit_interface, boost::get<Coprocessor::Callback>(action), inst);
        return;
    case 2: {
        auto source_ptrs = boost::get<std::array<u32*, 2>>(action);

        Xbyak::Reg64 reg_result = reg_alloc.ScratchGpr();
        Xbyak::Reg64 reg_destination_addr = reg_alloc.ScratchGpr();
        Xbyak::Reg64 reg_tmp = reg_alloc.ScratchGpr();

        code->mov(reg_destination_addr, reinterpret_cast<u64>(source_ptrs[1]));
        code->mov(reg_result.cvt32(), code->dword[reg_destination_addr]);
        code->shl(reg_result, 32);
        code->mov(reg_destination_addr, reinterpret_cast<u64>(source_ptrs[0]));
        code->mov(reg_tmp.cvt32(), code->dword[reg_destination_addr]);
        code->or_(reg_result, reg_tmp);

        reg_alloc.DefineValue(inst, reg_result);

        return;
    }
    default:
        ASSERT_MSG(false, "Unreachable");
    }
}

void A32EmitX64::EmitA32CoprocLoadWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    bool long_transfer = coproc_info[2] != 0;
    A32::CoprocReg CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    bool has_option = coproc_info[4] != 0;
    boost::optional<u8> option{has_option, coproc_info[5]};

    std::shared_ptr<Coprocessor> coproc = cb.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileLoadWords(two, long_transfer, CRd, option);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, reg_alloc, jit_interface, *action, nullptr, args[1]);
}

void A32EmitX64::EmitA32CoprocStoreWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    bool long_transfer = coproc_info[2] != 0;
    A32::CoprocReg CRd = static_cast<A32::CoprocReg>(coproc_info[3]);
    bool has_option = coproc_info[4] != 0;
    boost::optional<u8> option{has_option, coproc_info[5]};

    std::shared_ptr<Coprocessor> coproc = cb.coprocessors[coproc_num];
    if (!coproc) {
        EmitCoprocessorException();
        return;
    }

    auto action = coproc->CompileStoreWords(two, long_transfer, CRd, option);
    if (!action) {
        EmitCoprocessorException();
        return;
    }

    CallCoprocCallback(code, reg_alloc, jit_interface, *action, nullptr, args[1]);
}

void A32EmitX64::EmitTerminalImpl(IR::Term::Interpret terminal, IR::LocationDescriptor initial_location) {
    ASSERT_MSG(A32::LocationDescriptor{terminal.next}.TFlag() == A32::LocationDescriptor{initial_location}.TFlag(), "Unimplemented");
    ASSERT_MSG(A32::LocationDescriptor{terminal.next}.EFlag() == A32::LocationDescriptor{initial_location}.EFlag(), "Unimplemented");

    code->mov(code->ABI_PARAM1.cvt32(), A32::LocationDescriptor{terminal.next}.PC());
    code->mov(code->ABI_PARAM2, reinterpret_cast<u64>(jit_interface));
    code->mov(code->ABI_PARAM3, reinterpret_cast<u64>(cb.user_arg));
    code->mov(MJitStateReg(A32::Reg::PC), code->ABI_PARAM1.cvt32());
    code->SwitchMxcsrOnExit();
    code->CallFunction(cb.InterpreterFallback);
    code->ReturnFromRunCode(true); // TODO: Check cycles
}

void A32EmitX64::EmitTerminalImpl(IR::Term::ReturnToDispatch, IR::LocationDescriptor) {
    code->ReturnFromRunCode();
}

static u32 CalculateCpsr_et(const A32::LocationDescriptor& desc) {
    u32 et = 0;
    et |= desc.EFlag() ? 2 : 0;
    et |= desc.TFlag() ? 1 : 0;
    return et;
}

void A32EmitX64::EmitTerminalImpl(IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location) {
    if (CalculateCpsr_et(terminal.next) != CalculateCpsr_et(initial_location)) {
        code->mov(dword[r15 + offsetof(JitState, CPSR_et)], CalculateCpsr_et(terminal.next));
    }

    code->cmp(qword[r15 + offsetof(JitState, cycles_remaining)], 0);

    patch_information[terminal.next].jg.emplace_back(code->getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJg(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJg(terminal.next);
    }
    Xbyak::Label dest;
    code->jmp(dest, Xbyak::CodeGenerator::T_NEAR);

    code->SwitchToFarCode();
    code->align(16);
    code->L(dest);
    code->mov(MJitStateReg(A32::Reg::PC), A32::LocationDescriptor{terminal.next}.PC());
    PushRSBHelper(rax, rbx, terminal.next);
    code->ForceReturnFromRunCode();
    code->SwitchToNearCode();
}

void A32EmitX64::EmitTerminalImpl(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location) {
    if (CalculateCpsr_et(terminal.next) != CalculateCpsr_et(initial_location)) {
        code->mov(dword[r15 + offsetof(JitState, CPSR_et)], CalculateCpsr_et(terminal.next));
    }

    patch_information[terminal.next].jmp.emplace_back(code->getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJmp(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJmp(terminal.next);
    }
}

void A32EmitX64::EmitTerminalImpl(IR::Term::PopRSBHint, IR::LocationDescriptor) {
    // This calculation has to match up with IREmitter::PushRSB
    // TODO: Optimization is available here based on known state of FPSCR_mode and CPSR_et.
    code->mov(ecx, MJitStateReg(A32::Reg::PC));
    code->shl(rcx, 32);
    code->mov(ebx, dword[r15 + offsetof(JitState, FPSCR_mode)]);
    code->or_(ebx, dword[r15 + offsetof(JitState, CPSR_et)]);
    code->or_(rbx, rcx);

    code->mov(eax, dword[r15 + offsetof(JitState, rsb_ptr)]);
    code->sub(eax, 1);
    code->and_(eax, u32(JitState::RSBPtrMask));
    code->mov(dword[r15 + offsetof(JitState, rsb_ptr)], eax);
    code->cmp(rbx, qword[r15 + offsetof(JitState, rsb_location_descriptors) + rax * sizeof(u64)]);
    code->jne(code->GetReturnFromRunCodeAddress());
    code->mov(rax, qword[r15 + offsetof(JitState, rsb_codeptrs) + rax * sizeof(u64)]);
    code->jmp(rax);
}

void A32EmitX64::EmitTerminalImpl(IR::Term::If terminal, IR::LocationDescriptor initial_location) {
    Xbyak::Label pass = EmitCond(terminal.if_);
    EmitTerminal(terminal.else_, initial_location);
    code->L(pass);
    EmitTerminal(terminal.then_, initial_location);
}

void A32EmitX64::EmitTerminalImpl(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location) {
    code->cmp(code->byte[r15 + offsetof(JitState, halt_requested)], u8(0));
    code->jne(code->GetForceReturnFromRunCodeAddress());
    EmitTerminal(terminal.else_, initial_location);
}

void A32EmitX64::EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code->getCurr();
    if (target_code_ptr) {
        code->jg(target_code_ptr);
    } else {
        code->mov(MJitStateReg(A32::Reg::PC), A32::LocationDescriptor{target_desc}.PC());
        code->jg(code->GetReturnFromRunCodeAddress());
    }
    code->EnsurePatchLocationSize(patch_location, 14);
}

void A32EmitX64::EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code->getCurr();
    if (target_code_ptr) {
        code->jmp(target_code_ptr);
    } else {
        code->mov(MJitStateReg(A32::Reg::PC), A32::LocationDescriptor{target_desc}.PC());
        code->jmp(code->GetReturnFromRunCodeAddress());
    }
    code->EnsurePatchLocationSize(patch_location, 13);
}

void A32EmitX64::EmitPatchMovRcx(CodePtr target_code_ptr) {
    if (!target_code_ptr) {
        target_code_ptr = code->GetReturnFromRunCodeAddress();
    }
    const CodePtr patch_location = code->getCurr();
    code->mov(code->rcx, reinterpret_cast<u64>(target_code_ptr));
    code->EnsurePatchLocationSize(patch_location, 10);
}

} // namespace BackendX64
} // namespace Dynarmic
