/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <unordered_map>

#include <dynarmic/coprocessor.h>

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "backend_x64/jitstate.h"
#include "common/address_range.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/variant_util.h"
#include "frontend/arm/types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/location_descriptor.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

// TODO: Have ARM flags in host flags and not have them use up GPR registers unless necessary.
// TODO: Actually implement that proper instruction selector you've always wanted to sweetheart.

namespace Dynarmic {
namespace BackendX64 {

using namespace Xbyak::util;

constexpr u64 f32_negative_zero = 0x80000000u;
constexpr u64 f32_nan = 0x7fc00000u;
constexpr u64 f32_non_sign_mask = 0x7fffffffu;

constexpr u64 f64_negative_zero = 0x8000000000000000u;
constexpr u64 f64_nan = 0x7ff8000000000000u;
constexpr u64 f64_non_sign_mask = 0x7fffffffffffffffu;

constexpr u64 f64_penultimate_positive_denormal = 0x000ffffffffffffeu;
constexpr u64 f64_min_s32 = 0xc1e0000000000000u; // -2147483648 as a double
constexpr u64 f64_max_s32 = 0x41dfffffffc00000u; // 2147483647 as a double
constexpr u64 f64_min_u32 = 0x0000000000000000u; // 0 as a double

static Xbyak::Address MJitStateReg(Arm::Reg reg) {
    return dword[r15 + offsetof(JitState, Reg) + sizeof(u32) * static_cast<size_t>(reg)];
}

static Xbyak::Address MJitStateExtReg(Arm::ExtReg reg) {
    if (Arm::IsSingleExtReg(reg)) {
        size_t index = static_cast<size_t>(reg) - static_cast<size_t>(Arm::ExtReg::S0);
        return dword[r15 + offsetof(JitState, ExtReg) + sizeof(u32) * index];
    }
    if (Arm::IsDoubleExtReg(reg)) {
        size_t index = static_cast<size_t>(reg) - static_cast<size_t>(Arm::ExtReg::D0);
        return qword[r15 + offsetof(JitState, ExtReg) + sizeof(u64) * index];
    }
    ASSERT_MSG(false, "Should never happen.");
}

static void EraseInstruction(IR::Block& block, IR::Inst* inst) {
    block.Instructions().erase(inst);
    inst->Invalidate();
}

EmitX64::EmitX64(BlockOfCode* code, UserCallbacks cb, Jit* jit_interface)
    : code(code), cb(cb), jit_interface(jit_interface) {
}

EmitX64::BlockDescriptor EmitX64::Emit(IR::Block& block) {
    code->align();
    const u8* const entrypoint = code->getCurr();

    // Start emitting.
    EmitCondPrelude(block);

    RegAlloc reg_alloc{code};

    for (auto iter = block.begin(); iter != block.end(); ++iter) {
        IR::Inst* inst = &*iter;

        // Call the relevant Emit* member function.
        switch (inst->GetOpcode()) {

#define OPCODE(name, type, ...)                           \
        case IR::Opcode::name:                            \
            EmitX64::Emit##name(reg_alloc, block, inst);  \
            break;
#include "frontend/ir/opcodes.inc"
#undef OPCODE

        default:
            ASSERT_MSG(false, "Invalid opcode %zu", static_cast<size_t>(inst->GetOpcode()));
            break;
        }

        reg_alloc.EndOfAllocScope();
    }

    reg_alloc.AssertNoMoreUses();

    EmitAddCycles(block.CycleCount());
    EmitTerminal(block.GetTerminal(), block.Location());
    code->int3();

    const IR::LocationDescriptor descriptor = block.Location();
    Patch(descriptor, entrypoint);

    const size_t size = static_cast<size_t>(code->getCurr() - entrypoint);
    EmitX64::BlockDescriptor block_desc{entrypoint, size, block.Location(), block.EndLocation().PC()};
    block_descriptors.emplace(descriptor.UniqueHash(), block_desc);

    block_ranges.add(std::make_pair(boost::icl::discrete_interval<u32>::closed(block.Location().PC(), block.EndLocation().PC() - 1), std::set<IR::LocationDescriptor>{descriptor}));

    return block_desc;
}

boost::optional<EmitX64::BlockDescriptor> EmitX64::GetBasicBlock(IR::LocationDescriptor descriptor) const {
    auto iter = block_descriptors.find(descriptor.UniqueHash());
    if (iter == block_descriptors.end())
        return boost::none;
    return iter->second;
}

void EmitX64::EmitVoid(RegAlloc&, IR::Block&, IR::Inst*) {
}

void EmitX64::EmitBreakpoint(RegAlloc&, IR::Block&, IR::Inst*) {
    code->int3();
}

void EmitX64::EmitIdentity(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (!args[0].IsImmediate()) {
        reg_alloc.DefineValue(inst, args[0]);
    }
}

void EmitX64::EmitGetRegister(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Arm::Reg reg = inst->GetArg(0).GetRegRef();

    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, MJitStateReg(reg));
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitGetExtendedRegister32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Arm::ExtReg reg = inst->GetArg(0).GetExtRegRef();
    ASSERT(Arm::IsSingleExtReg(reg));

    Xbyak::Xmm result = reg_alloc.ScratchXmm();
    code->movss(result, MJitStateExtReg(reg));
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitGetExtendedRegister64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Arm::ExtReg reg = inst->GetArg(0).GetExtRegRef();
    ASSERT(Arm::IsDoubleExtReg(reg));

    Xbyak::Xmm result = reg_alloc.ScratchXmm();
    code->movsd(result, MJitStateExtReg(reg));
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSetRegister(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Arm::Reg reg = inst->GetArg(0).GetRegRef();
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

void EmitX64::EmitSetExtendedRegister32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Arm::ExtReg reg = inst->GetArg(0).GetExtRegRef();
    ASSERT(Arm::IsSingleExtReg(reg));
    if (args[1].IsInXmm()) {
        Xbyak::Xmm to_store = reg_alloc.UseXmm(args[1]);
        code->movss(MJitStateExtReg(reg), to_store);
    } else {
        Xbyak::Reg32 to_store = reg_alloc.UseGpr(args[1]).cvt32();
        code->mov(MJitStateExtReg(reg), to_store);
    }
}

void EmitX64::EmitSetExtendedRegister64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Arm::ExtReg reg = inst->GetArg(0).GetExtRegRef();
    ASSERT(Arm::IsDoubleExtReg(reg));
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

void EmitX64::EmitGetCpsr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitSetCpsr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.HostCall(nullptr, args[0]);
    code->mov(code->ABI_PARAM2, code->r15);
    code->CallFunction(&SetCpsrImpl);
}

void EmitX64::EmitSetCpsrNZCV(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitSetCpsrNZCVQ(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitGetNFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 31);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSetNFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitGetZFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 30);
    code->and_(result, 1);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSetZFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitGetCFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 29);
    code->and_(result, 1);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSetCFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitGetVFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, CPSR_nzcv)]);
    code->shr(result, 28);
    code->and_(result, 1);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSetVFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitOrQFlag(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate()) {
        if (args[0].GetImmediateU1())
            code->mov(dword[r15 + offsetof(JitState, CPSR_q)], 1);
    } else {
        Xbyak::Reg8 to_store = reg_alloc.UseGpr(args[0]).cvt8();

        code->or_(code->byte[r15 + offsetof(JitState, CPSR_q)], to_store);
    }
}

void EmitX64::EmitGetGEFlags(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Xmm result = reg_alloc.ScratchXmm();
    code->movd(result, dword[r15 + offsetof(JitState, CPSR_ge)]);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSetGEFlags(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitSetGEFlagsCompressed(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitBXWritePC(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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
        et |= block.Location().EFlag() ? 2 : 0;
        et |= Common::Bit<0>(new_pc) ? 1 : 0;

        code->mov(MJitStateReg(Arm::Reg::PC), new_pc & mask);
        code->mov(dword[r15 + offsetof(JitState, CPSR_et)], et);
    } else {
        if (block.Location().EFlag()) {
            Xbyak::Reg32 new_pc = reg_alloc.UseScratchGpr(arg).cvt32();
            Xbyak::Reg32 mask = reg_alloc.ScratchGpr().cvt32();
            Xbyak::Reg32 et = reg_alloc.ScratchGpr().cvt32();

            code->mov(mask, new_pc);
            code->and_(mask, 1);
            code->lea(et, ptr[mask.cvt64() + 2]);
            code->mov(dword[r15 + offsetof(JitState, CPSR_et)], et);
            code->lea(mask, ptr[mask.cvt64() + mask.cvt64() * 1 - 4]); // mask = pc & 1 ? 0xFFFFFFFE : 0xFFFFFFFC
            code->and_(new_pc, mask);
            code->mov(MJitStateReg(Arm::Reg::PC), new_pc);
        } else {
            Xbyak::Reg32 new_pc = reg_alloc.UseScratchGpr(arg).cvt32();
            Xbyak::Reg32 mask = reg_alloc.ScratchGpr().cvt32();

            code->mov(mask, new_pc);
            code->and_(mask, 1);
            code->mov(dword[r15 + offsetof(JitState, CPSR_et)], mask);
            code->lea(mask, ptr[mask.cvt64() + mask.cvt64() * 1 - 4]); // mask = pc & 1 ? 0xFFFFFFFE : 0xFFFFFFFC
            code->and_(new_pc, mask);
            code->mov(MJitStateReg(Arm::Reg::PC), new_pc);
        }
    }
}

void EmitX64::EmitCallSupervisor(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitGetFpscr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    reg_alloc.HostCall(inst);
    code->mov(code->ABI_PARAM1, code->r15);

    code->stmxcsr(code->dword[code->r15 + offsetof(JitState, guest_MXCSR)]);
    code->CallFunction(&GetFpscrImpl);
}

static void SetFpscrImpl(u32 value, JitState* jit_state) {
    jit_state->SetFpscr(value);
}

void EmitX64::EmitSetFpscr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.HostCall(nullptr, args[0]);
    code->mov(code->ABI_PARAM2, code->r15);

    code->CallFunction(&SetFpscrImpl);
    code->ldmxcsr(code->dword[code->r15 + offsetof(JitState, guest_MXCSR)]);
}

void EmitX64::EmitGetFpscrNZCV(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    code->mov(result, dword[r15 + offsetof(JitState, FPSCR_nzcv)]);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSetFpscrNZCV(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 value = reg_alloc.UseGpr(args[0]).cvt32();

    code->mov(dword[r15 + offsetof(JitState, FPSCR_nzcv)], value);
}

void EmitX64::PushRSBHelper(Xbyak::Reg64 loc_desc_reg, Xbyak::Reg64 index_reg, u64 target_hash) {
    using namespace Xbyak::util;

    auto iter = block_descriptors.find(target_hash);
    CodePtr target_code_ptr = iter != block_descriptors.end()
                            ? iter->second.entrypoint
                            : code->GetReturnFromRunCodeAddress();

    code->mov(index_reg.cvt32(), dword[r15 + offsetof(JitState, rsb_ptr)]);

    code->mov(loc_desc_reg, target_hash);

    patch_information[target_hash].mov_rcx.emplace_back(code->getCurr());
    EmitPatchMovRcx(target_code_ptr);

    code->mov(qword[r15 + index_reg * 8 + offsetof(JitState, rsb_location_descriptors)], loc_desc_reg);
    code->mov(qword[r15 + index_reg * 8 + offsetof(JitState, rsb_codeptrs)], rcx);

    code->add(index_reg.cvt32(), 1);
    code->and_(index_reg.cvt32(), u32(JitState::RSBPtrMask));
    code->mov(dword[r15 + offsetof(JitState, rsb_ptr)], index_reg.cvt32());
}

void EmitX64::EmitPushRSB(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    u64 unique_hash_of_target = args[0].GetImmediateU64();

    reg_alloc.ScratchGpr({HostLoc::RCX});
    Xbyak::Reg64 loc_desc_reg = reg_alloc.ScratchGpr();
    Xbyak::Reg64 index_reg = reg_alloc.ScratchGpr();

    PushRSBHelper(loc_desc_reg, index_reg, unique_hash_of_target);
}

void EmitX64::EmitGetCarryFromOp(RegAlloc&, IR::Block&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

void EmitX64::EmitGetOverflowFromOp(RegAlloc&, IR::Block&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

void EmitX64::EmitGetGEFromOp(RegAlloc&, IR::Block&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

void EmitX64::EmitPack2x32To1x64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 lo = reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg64 hi = reg_alloc.UseScratchGpr(args[1]);

    code->shl(hi, 32);
    code->mov(lo.cvt32(), lo.cvt32()); // Zero extend to 64-bits
    code->or_(lo, hi);

    reg_alloc.DefineValue(inst, lo);
}

void EmitX64::EmitLeastSignificantWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

void EmitX64::EmitMostSignificantWord(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->shr(result, 32);

    if (carry_inst) {
        EraseInstruction(block, carry_inst);
        Xbyak::Reg64 carry = reg_alloc.ScratchGpr();
        code->setc(carry.cvt8());
        reg_alloc.DefineValue(carry_inst, carry);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitLeastSignificantHalf(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

void EmitX64::EmitLeastSignificantByte(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

void EmitX64::EmitMostSignificantBit(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    // TODO: Flag optimization
    code->shr(result, 31);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitIsZero(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    // TODO: Flag optimization
    code->test(result, result);
    code->sete(result.cvt8());
    code->movzx(result, result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitIsZero64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    // TODO: Flag optimization
    code->test(result, result);
    code->sete(result.cvt8());
    code->movzx(result, result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitLogicalShiftLeft(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    // TODO: Consider using BMI2 instructions like SHLX when arm-in-host flags is implemented.

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            u8 shift = shift_arg.GetImmediateU8();

            if (shift <= 31) {
                code->shl(result, shift);
            } else {
                code->xor_(result, result);
            }

            reg_alloc.DefineValue(inst, result);
        } else {
            reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 zero = reg_alloc.ScratchGpr().cvt32();

            // The 32-bit x64 SHL instruction masks the shift count by 0x1F before performing the shift.
            // ARM differs from the behaviour: It does not mask the count, so shifts above 31 result in zeros.

            code->shl(result, code->cl);
            code->xor_(zero, zero);
            code->cmp(code->cl, 32);
            code->cmovnb(result, zero);

            reg_alloc.DefineValue(inst, result);
        }
    } else {
        EraseInstruction(block, carry_inst);

        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = reg_alloc.UseScratchGpr(carry_arg).cvt32();

            if (shift == 0) {
                // There is nothing more to do.
            } else if (shift < 32) {
                code->bt(carry.cvt32(), 0);
                code->shl(result, shift);
                code->setc(carry.cvt8());
            } else if (shift > 32) {
                code->xor_(result, result);
                code->xor_(carry, carry);
            } else {
                code->mov(carry, result);
                code->xor_(result, result);
                code->and_(carry, 1);
            }

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        } else {
            reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = reg_alloc.UseScratchGpr(carry_arg).cvt32();

            // TODO: Optimize this.

            code->inLocalLabel();

            code->cmp(code->cl, 32);
            code->ja(".Rs_gt32");
            code->je(".Rs_eq32");
            // if (Rs & 0xFF < 32) {
            code->bt(carry.cvt32(), 0); // Set the carry flag for correct behaviour in the case when Rs & 0xFF == 0
            code->shl(result, code->cl);
            code->setc(carry.cvt8());
            code->jmp(".end");
            // } else if (Rs & 0xFF > 32) {
            code->L(".Rs_gt32");
            code->xor_(result, result);
            code->xor_(carry, carry);
            code->jmp(".end");
            // } else if (Rs & 0xFF == 32) {
            code->L(".Rs_eq32");
            code->mov(carry, result);
            code->and_(carry, 1);
            code->xor_(result, result);
            // }
            code->L(".end");

            code->outLocalLabel();

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        }
    }
}

void EmitX64::EmitLogicalShiftRight(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            u8 shift = shift_arg.GetImmediateU8();

            if (shift <= 31) {
                code->shr(result, shift);
            } else {
                code->xor_(result, result);
            }

            reg_alloc.DefineValue(inst, result);
        } else {
            reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 zero = reg_alloc.ScratchGpr().cvt32();

            // The 32-bit x64 SHR instruction masks the shift count by 0x1F before performing the shift.
            // ARM differs from the behaviour: It does not mask the count, so shifts above 31 result in zeros.

            code->shr(result, code->cl);
            code->xor_(zero, zero);
            code->cmp(code->cl, 32);
            code->cmovnb(result, zero);

            reg_alloc.DefineValue(inst, result);
        }
    } else {
        EraseInstruction(block, carry_inst);

        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = reg_alloc.UseScratchGpr(carry_arg).cvt32();

            if (shift == 0) {
                // There is nothing more to do.
            } else if (shift < 32) {
                code->shr(result, shift);
                code->setc(carry.cvt8());
            } else if (shift == 32) {
                code->bt(result, 31);
                code->setc(carry.cvt8());
                code->mov(result, 0);
            } else {
                code->xor_(result, result);
                code->xor_(carry, carry);
            }

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        } else {
            reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = reg_alloc.UseScratchGpr(carry_arg).cvt32();

            // TODO: Optimize this.

            code->inLocalLabel();

            code->cmp(code->cl, 32);
            code->ja(".Rs_gt32");
            code->je(".Rs_eq32");
            // if (Rs & 0xFF == 0) goto end;
            code->test(code->cl, code->cl);
            code->jz(".end");
            // if (Rs & 0xFF < 32) {
            code->shr(result, code->cl);
            code->setc(carry.cvt8());
            code->jmp(".end");
            // } else if (Rs & 0xFF > 32) {
            code->L(".Rs_gt32");
            code->xor_(result, result);
            code->xor_(carry, carry);
            code->jmp(".end");
            // } else if (Rs & 0xFF == 32) {
            code->L(".Rs_eq32");
            code->bt(result, 31);
            code->setc(carry.cvt8());
            code->xor_(result, result);
            // }
            code->L(".end");

            code->outLocalLabel();

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        }
    }
}

void EmitX64::EmitLogicalShiftRight64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];

    ASSERT_MSG(shift_arg.IsImmediate(), "variable 64 bit shifts are not implemented");
    ASSERT_MSG(shift_arg.GetImmediateU8() < 64, "shift width clamping is not implemented");

    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(operand_arg);
    u8 shift = shift_arg.GetImmediateU8();

    code->shr(result.cvt64(), shift);

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitArithmeticShiftRight(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();

            code->sar(result, u8(shift < 31 ? shift : 31));

            reg_alloc.DefineValue(inst, result);
        } else {
            reg_alloc.UseScratch(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 const31 = reg_alloc.ScratchGpr().cvt32();

            // The 32-bit x64 SAR instruction masks the shift count by 0x1F before performing the shift.
            // ARM differs from the behaviour: It does not mask the count.

            // We note that all shift values above 31 have the same behaviour as 31 does, so we saturate `shift` to 31.
            code->mov(const31, 31);
            code->movzx(code->ecx, code->cl);
            code->cmp(code->ecx, u32(31));
            code->cmovg(code->ecx, const31);
            code->sar(result, code->cl);

            reg_alloc.DefineValue(inst, result);
        }
    } else {
        EraseInstruction(block, carry_inst);

        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = reg_alloc.UseScratchGpr(carry_arg).cvt8();

            if (shift == 0) {
                // There is nothing more to do.
            } else if (shift <= 31) {
                code->sar(result, shift);
                code->setc(carry);
            } else {
                code->sar(result, 31);
                code->bt(result, 31);
                code->setc(carry);
            }

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        } else {
            reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = reg_alloc.UseScratchGpr(carry_arg).cvt8();

            // TODO: Optimize this.

            code->inLocalLabel();

            code->cmp(code->cl, u32(31));
            code->ja(".Rs_gt31");
            // if (Rs & 0xFF == 0) goto end;
            code->test(code->cl, code->cl);
            code->jz(".end");
            // if (Rs & 0xFF <= 31) {
            code->sar(result, code->cl);
            code->setc(carry);
            code->jmp(".end");
            // } else if (Rs & 0xFF > 31) {
            code->L(".Rs_gt31");
            code->sar(result, 31); // 31 produces the same results as anything above 31
            code->bt(result, 31);
            code->setc(carry);
            // }
            code->L(".end");

            code->outLocalLabel();

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        }
    }
}

void EmitX64::EmitRotateRight(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();

            code->ror(result, u8(shift & 0x1F));

            reg_alloc.DefineValue(inst, result);
        } else {
            reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();

            // x64 ROR instruction does (shift & 0x1F) for us.
            code->ror(result, code->cl);

            reg_alloc.DefineValue(inst, result);
        }
    } else {
        EraseInstruction(block, carry_inst);

        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = reg_alloc.UseScratchGpr(carry_arg).cvt8();

            if (shift == 0) {
                // There is nothing more to do.
            } else if ((shift & 0x1F) == 0) {
                code->bt(result, u8(31));
                code->setc(carry);
            } else {
                code->ror(result, shift);
                code->setc(carry);
            }

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        } else {
            reg_alloc.UseScratch(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = reg_alloc.UseScratchGpr(carry_arg).cvt8();

            // TODO: Optimize

            code->inLocalLabel();

            // if (Rs & 0xFF == 0) goto end;
            code->test(code->cl, code->cl);
            code->jz(".end");

            code->and_(code->ecx, u32(0x1F));
            code->jz(".zero_1F");
            // if (Rs & 0x1F != 0) {
            code->ror(result, code->cl);
            code->setc(carry);
            code->jmp(".end");
            // } else {
            code->L(".zero_1F");
            code->bt(result, u8(31));
            code->setc(carry);
            // }
            code->L(".end");

            code->outLocalLabel();

            reg_alloc.DefineValue(inst, result);
            reg_alloc.DefineValue(carry_inst, carry);
        }
    }
}

void EmitX64::EmitRotateRightExtended(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg8 carry = reg_alloc.UseScratchGpr(args[1]).cvt8();

    code->bt(carry.cvt32(), 0);
    code->rcr(result, 1);

    if (carry_inst) {
        EraseInstruction(block, carry_inst);

        code->setc(carry);

        reg_alloc.DefineValue(carry_inst, carry);
    }

    reg_alloc.DefineValue(inst, result);
}

const Xbyak::Reg64 INVALID_REG = Xbyak::Reg64(-1);

static Xbyak::Reg8 DoCarry(RegAlloc& reg_alloc, Argument& carry_in, IR::Inst* carry_out) {
    if (carry_in.IsImmediate()) {
        return carry_out ? reg_alloc.ScratchGpr().cvt8() : INVALID_REG.cvt8();
    } else {
        return carry_out ? reg_alloc.UseScratchGpr(carry_in).cvt8() : reg_alloc.UseGpr(carry_in).cvt8();
    }
}

void EmitX64::EmitAddWithCarry(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    auto& carry_in = args[2];

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg8 carry = DoCarry(reg_alloc, carry_in, carry_inst);
    Xbyak::Reg8 overflow = overflow_inst ? reg_alloc.ScratchGpr().cvt8() : INVALID_REG.cvt8();

    // TODO: Consider using LEA.

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();
        if (carry_in.IsImmediate()) {
            if (carry_in.GetImmediateU1()) {
                code->stc();
                code->adc(result, op_arg);
            } else {
                code->add(result, op_arg);
            }
        } else {
            code->bt(carry.cvt32(), 0);
            code->adc(result, op_arg);
        }
    } else {
        OpArg op_arg = reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);
        if (carry_in.IsImmediate()) {
            if (carry_in.GetImmediateU1()) {
                code->stc();
                code->adc(result, *op_arg);
            } else {
                code->add(result, *op_arg);
            }
        } else {
            code->bt(carry.cvt32(), 0);
            code->adc(result, *op_arg);
        }
    }

    if (carry_inst) {
        EraseInstruction(block, carry_inst);
        code->setc(carry);
        reg_alloc.DefineValue(carry_inst, carry);
    }
    if (overflow_inst) {
        EraseInstruction(block, overflow_inst);
        code->seto(overflow);
        reg_alloc.DefineValue(overflow_inst, overflow);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitAdd64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg64 op_arg = reg_alloc.UseGpr(args[1]);

    code->add(result, op_arg);

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSubWithCarry(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    auto& carry_in = args[2];

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg8 carry = DoCarry(reg_alloc, carry_in, carry_inst);
    Xbyak::Reg8 overflow = overflow_inst ? reg_alloc.ScratchGpr().cvt8() : INVALID_REG.cvt8();

    // TODO: Consider using LEA.
    // TODO: Optimize CMP case.
    // Note that x64 CF is inverse of what the ARM carry flag is here.

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();
        if (carry_in.IsImmediate()) {
            if (carry_in.GetImmediateU1()) {
                code->sub(result, op_arg);
            } else {
                code->stc();
                code->sbb(result, op_arg);
            }
        } else {
            code->bt(carry.cvt32(), 0);
            code->cmc();
            code->sbb(result, op_arg);
        }
    } else {
        OpArg op_arg = reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);
        if (carry_in.IsImmediate()) {
            if (carry_in.GetImmediateU1()) {
                code->sub(result, *op_arg);
            } else {
                code->stc();
                code->sbb(result, *op_arg);
            }
        } else {
            code->bt(carry.cvt32(), 0);
            code->cmc();
            code->sbb(result, *op_arg);
        }
    }

    if (carry_inst) {
        EraseInstruction(block, carry_inst);
        code->setnc(carry);
        reg_alloc.DefineValue(carry_inst, carry);
    }
    if (overflow_inst) {
        EraseInstruction(block, overflow_inst);
        code->seto(overflow);
        reg_alloc.DefineValue(overflow_inst, overflow);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSub64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg64 op_arg = reg_alloc.UseGpr(args[1]);

    code->sub(result, op_arg);

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitMul(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    if (args[1].IsImmediate()) {
        code->imul(result, result, args[1].GetImmediateU32());
    } else {
        OpArg op_arg = reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->imul(result, *op_arg);
    }
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitMul64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    OpArg op_arg = reg_alloc.UseOpArg(args[1]);

    code->imul(result, *op_arg);

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitAnd(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();

        code->and_(result, op_arg);
    } else {
        OpArg op_arg = reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->and_(result, *op_arg);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitEor(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();

        code->xor_(result, op_arg);
    } else {
        OpArg op_arg = reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->xor_(result, *op_arg);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitOr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();

        code->or_(result, op_arg);
    } else {
        OpArg op_arg = reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->or_(result, *op_arg);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitNot(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result;
    if (args[0].IsImmediate()) {
        result = reg_alloc.ScratchGpr().cvt32();
        code->mov(result, u32(~args[0].GetImmediateU32()));
    } else {
        result = reg_alloc.UseScratchGpr(args[0]).cvt32();
        code->not_(result);
    }
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSignExtendWordToLong(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movsxd(result.cvt64(), result.cvt32());
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSignExtendHalfToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt32(), result.cvt16());
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSignExtendByteToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt32(), result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitZeroExtendWordToLong(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->mov(result.cvt32(), result.cvt32()); // x64 zeros upper 32 bits on a 32-bit move
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitZeroExtendHalfToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt16());
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitZeroExtendByteToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitByteReverseWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    code->bswap(result);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitByteReverseHalf(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg16 result = reg_alloc.UseScratchGpr(args[0]).cvt16();
    code->rol(result, 8);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitByteReverseDual(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->bswap(result);
    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitCountLeadingZeros(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (code->DoesCpuSupport(Xbyak::util::Cpu::tLZCNT)) {
        Xbyak::Reg32 source = reg_alloc.UseGpr(args[0]).cvt32();
        Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();

        code->lzcnt(result, source);

        reg_alloc.DefineValue(inst, result);
    } else {
        Xbyak::Reg32 source = reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();

        // The result of a bsr of zero is undefined, but zf is set after it.
        code->bsr(result, source);
        code->mov(source, 0xFFFFFFFF);
        code->cmovz(result, source);
        code->neg(result);
        code->add(result, 31);

        reg_alloc.DefineValue(inst, result);
    }
}

void EmitX64::EmitSignedSaturatedAdd(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 addend = reg_alloc.UseGpr(args[1]).cvt32();
    Xbyak::Reg32 overflow = reg_alloc.ScratchGpr().cvt32();

    code->mov(overflow, result);
    code->shr(overflow, 31);
    code->add(overflow, 0x7FFFFFFF);
    // overflow now contains 0x7FFFFFFF if a was positive, or 0x80000000 if a was negative
    code->add(result, addend);
    code->cmovo(result, overflow);

    if (overflow_inst) {
        EraseInstruction(block, overflow_inst);

        code->seto(overflow.cvt8());

        reg_alloc.DefineValue(overflow_inst, overflow);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSignedSaturatedSub(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 subend = reg_alloc.UseGpr(args[1]).cvt32();
    Xbyak::Reg32 overflow = reg_alloc.ScratchGpr().cvt32();

    code->mov(overflow, result);
    code->shr(overflow, 31);
    code->add(overflow, 0x7FFFFFFF);
    // overflow now contains 0x7FFFFFFF if a was positive, or 0x80000000 if a was negative
    code->sub(result, subend);
    code->cmovo(result, overflow);

    if (overflow_inst) {
        EraseInstruction(block, overflow_inst);

        code->seto(overflow.cvt8());

        reg_alloc.DefineValue(overflow_inst, overflow);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitUnsignedSaturation(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    size_t N = args[1].GetImmediateU8();
    ASSERT(N <= 31);

    u32 saturated_value = (1u << N) - 1;

    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 reg_a = reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Reg32 overflow = reg_alloc.ScratchGpr().cvt32();

    // Pseudocode: result = clamp(reg_a, 0, saturated_value);
    code->xor_(overflow, overflow);
    code->cmp(reg_a, saturated_value);
    code->mov(result, saturated_value);
    code->cmovle(result, overflow);
    code->cmovbe(result, reg_a);

    if (overflow_inst) {
        EraseInstruction(block, overflow_inst);

        code->seta(overflow.cvt8());

        reg_alloc.DefineValue(overflow_inst, overflow);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSignedSaturation(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = reg_alloc.GetArgumentInfo(inst);
    size_t N = args[1].GetImmediateU8();
    ASSERT(N >= 1 && N <= 32);

    if (N == 32) {
        if (overflow_inst) {
            auto no_overflow = IR::Value(false);
            overflow_inst->ReplaceUsesWith(no_overflow);
        }
        reg_alloc.DefineValue(inst, args[0]);
        return;
    }

    u32 mask = (1u << N) - 1;
    u32 positive_saturated_value = (1u << (N - 1)) - 1;
    u32 negative_saturated_value = 1u << (N - 1);
    u32 sext_negative_satured_value = Common::SignExtend(N, negative_saturated_value);

    Xbyak::Reg32 result = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 reg_a = reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Reg32 overflow = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 tmp = reg_alloc.ScratchGpr().cvt32();

    // overflow now contains a value between 0 and mask if it was originally between {negative,positive}_saturated_value.
    code->lea(overflow, code->ptr[reg_a.cvt64() + negative_saturated_value]);

    // Put the appropriate saturated value in result
    code->cmp(reg_a, positive_saturated_value);
    code->mov(tmp, positive_saturated_value);
    code->mov(result, sext_negative_satured_value);
    code->cmovg(result, tmp);

    // Do the saturation
    code->cmp(overflow, mask);
    code->cmovbe(result, reg_a);

    if (overflow_inst) {
        EraseInstruction(block, overflow_inst);

        code->seta(overflow.cvt8());

        reg_alloc.DefineValue(overflow_inst, overflow);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitPackedAddU8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    code->paddb(xmm_a, xmm_b);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();
        Xbyak::Xmm ones = reg_alloc.ScratchXmm();

        code->pcmpeqb(ones, ones);

        code->movdqa(xmm_ge, xmm_a);
        code->pminub(xmm_ge, xmm_b);
        code->pcmpeqb(xmm_ge, xmm_b);
        code->pxor(xmm_ge, ones);

        reg_alloc.DefineValue(ge_inst, xmm_ge);
    }

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedAddS8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        Xbyak::Xmm saturated_sum = reg_alloc.ScratchXmm();
        Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();

        code->pxor(xmm_ge, xmm_ge);
        code->movdqa(saturated_sum, xmm_a);
        code->paddsb(saturated_sum, xmm_b);
        code->pcmpgtb(xmm_ge, saturated_sum);
        code->pcmpeqb(saturated_sum, saturated_sum);
        code->pxor(xmm_ge, saturated_sum);

        reg_alloc.DefineValue(ge_inst, xmm_ge);
    }

    code->paddb(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedAddU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    code->paddw(xmm_a, xmm_b);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
            Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();
            Xbyak::Xmm ones = reg_alloc.ScratchXmm();

            code->pcmpeqb(ones, ones);

            code->movdqa(xmm_ge, xmm_a);
            code->pminuw(xmm_ge, xmm_b);
            code->pcmpeqw(xmm_ge, xmm_b);
            code->pxor(xmm_ge, ones);

            reg_alloc.DefineValue(ge_inst, xmm_ge);
        } else {
            Xbyak::Xmm tmp_a = reg_alloc.ScratchXmm();
            Xbyak::Xmm tmp_b = reg_alloc.ScratchXmm();

            // !(b <= a+b) == b > a+b
            code->movdqa(tmp_a, xmm_a);
            code->movdqa(tmp_b, xmm_b);
            code->paddw(tmp_a, code->MConst(0x80008000));
            code->paddw(tmp_b, code->MConst(0x80008000));
            code->pcmpgtw(tmp_b, tmp_a); // *Signed* comparison!

            reg_alloc.DefineValue(ge_inst, tmp_b);
        }
    }

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedAddS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        Xbyak::Xmm saturated_sum = reg_alloc.ScratchXmm();
        Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();

        code->pxor(xmm_ge, xmm_ge);
        code->movdqa(saturated_sum, xmm_a);
        code->paddsw(saturated_sum, xmm_b);
        code->pcmpgtw(xmm_ge, saturated_sum);
        code->pcmpeqw(saturated_sum, saturated_sum);
        code->pxor(xmm_ge, saturated_sum);

        reg_alloc.DefineValue(ge_inst, xmm_ge);
    }

    code->paddw(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedSubU8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();

        code->movdqa(xmm_ge, xmm_a);
        code->pmaxub(xmm_ge, xmm_b);
        code->pcmpeqb(xmm_ge, xmm_a);

        reg_alloc.DefineValue(ge_inst, xmm_ge);
    }

    code->psubb(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}


void EmitX64::EmitPackedSubS8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        Xbyak::Xmm saturated_sum = reg_alloc.ScratchXmm();
        Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();

        code->pxor(xmm_ge, xmm_ge);
        code->movdqa(saturated_sum, xmm_a);
        code->psubsb(saturated_sum, xmm_b);
        code->pcmpgtb(xmm_ge, saturated_sum);
        code->pcmpeqb(saturated_sum, saturated_sum);
        code->pxor(xmm_ge, saturated_sum);

        reg_alloc.DefineValue(ge_inst, xmm_ge);
    }

    code->psubb(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedSubU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        if (code->DoesCpuSupport(Xbyak::util::Cpu::tSSE41)) {
            Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();

            code->movdqa(xmm_ge, xmm_a);
            code->pmaxuw(xmm_ge, xmm_b); // Requires SSE 4.1
            code->pcmpeqw(xmm_ge, xmm_a);

            reg_alloc.DefineValue(ge_inst, xmm_ge);
        } else {
            Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();
            Xbyak::Xmm ones = reg_alloc.ScratchXmm();

            // (a >= b) == !(b > a)
            code->pcmpeqb(ones, ones);
            code->paddw(xmm_a, code->MConst(0x80008000));
            code->paddw(xmm_b, code->MConst(0x80008000));
            code->movdqa(xmm_ge, xmm_b);
            code->pcmpgtw(xmm_ge, xmm_a); // *Signed* comparison!
            code->pxor(xmm_ge, ones);

            reg_alloc.DefineValue(ge_inst, xmm_ge);
        }
    }

    code->psubw(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedSubS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        Xbyak::Xmm saturated_diff = reg_alloc.ScratchXmm();
        Xbyak::Xmm xmm_ge = reg_alloc.ScratchXmm();

        code->pxor(xmm_ge, xmm_ge);
        code->movdqa(saturated_diff, xmm_a);
        code->psubsw(saturated_diff, xmm_b);
        code->pcmpgtw(xmm_ge, saturated_diff);
        code->pcmpeqw(saturated_diff, saturated_diff);
        code->pxor(xmm_ge, saturated_diff);

        reg_alloc.DefineValue(ge_inst, xmm_ge);
    }

    code->psubw(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedHalvingAddU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    if (args[0].IsInXmm() || args[1].IsInXmm()) {
        Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = reg_alloc.UseScratchXmm(args[1]);
        Xbyak::Xmm ones = reg_alloc.ScratchXmm();

        // Since,
        //   pavg(a, b) == (a + b + 1) >> 1
        // Therefore,
        //   ~pavg(~a, ~b) == (a + b) >> 1

        code->pcmpeqb(ones, ones);
        code->pxor(xmm_a, ones);
        code->pxor(xmm_b, ones);
        code->pavgb(xmm_a, xmm_b);
        code->pxor(xmm_a, ones);

        reg_alloc.DefineValue(inst, xmm_a);
    } else {
        Xbyak::Reg32 reg_a = reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 reg_b = reg_alloc.UseGpr(args[1]).cvt32();
        Xbyak::Reg32 xor_a_b = reg_alloc.ScratchGpr().cvt32();
        Xbyak::Reg32 and_a_b = reg_a;
        Xbyak::Reg32 result = reg_a;

        // This relies on the equality x+y == ((x&y) << 1) + (x^y).
        // Note that x^y always contains the LSB of the result.
        // Since we want to calculate (x+y)/2, we can instead calculate (x&y) + ((x^y)>>1).
        // We mask by 0x7F to remove the LSB so that it doesn't leak into the field below.

        code->mov(xor_a_b, reg_a);
        code->and_(and_a_b, reg_b);
        code->xor_(xor_a_b, reg_b);
        code->shr(xor_a_b, 1);
        code->and_(xor_a_b, 0x7F7F7F7F);
        code->add(result, xor_a_b);

        reg_alloc.DefineValue(inst, result);
    }
}

void EmitX64::EmitPackedHalvingAddU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    if (args[0].IsInXmm() || args[1].IsInXmm()) {
        Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);
        Xbyak::Xmm tmp = reg_alloc.ScratchXmm();

        code->movdqa(tmp, xmm_a);
        code->pand(xmm_a, xmm_b);
        code->pxor(tmp, xmm_b);
        code->psrlw(tmp, 1);
        code->paddw(xmm_a, tmp);

        reg_alloc.DefineValue(inst, xmm_a);
    } else {
        Xbyak::Reg32 reg_a = reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 reg_b = reg_alloc.UseGpr(args[1]).cvt32();
        Xbyak::Reg32 xor_a_b = reg_alloc.ScratchGpr().cvt32();
        Xbyak::Reg32 and_a_b = reg_a;
        Xbyak::Reg32 result = reg_a;

        // This relies on the equality x+y == ((x&y) << 1) + (x^y).
        // Note that x^y always contains the LSB of the result.
        // Since we want to calculate (x+y)/2, we can instead calculate (x&y) + ((x^y)>>1).
        // We mask by 0x7FFF to remove the LSB so that it doesn't leak into the field below.

        code->mov(xor_a_b, reg_a);
        code->and_(and_a_b, reg_b);
        code->xor_(xor_a_b, reg_b);
        code->shr(xor_a_b, 1);
        code->and_(xor_a_b, 0x7FFF7FFF);
        code->add(result, xor_a_b);

        reg_alloc.DefineValue(inst, result);
    }
}

void EmitX64::EmitPackedHalvingAddS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 reg_a = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 reg_b = reg_alloc.UseGpr(args[1]).cvt32();
    Xbyak::Reg32 xor_a_b = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 and_a_b = reg_a;
    Xbyak::Reg32 result = reg_a;
    Xbyak::Reg32 carry = reg_alloc.ScratchGpr().cvt32();

    // This relies on the equality x+y == ((x&y) << 1) + (x^y).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x+y)/2, we can instead calculate (x&y) + ((x^y)>>1).
    // We mask by 0x7F to remove the LSB so that it doesn't leak into the field below.
    // carry propagates the sign bit from (x^y)>>1 upwards by one.

    code->mov(xor_a_b, reg_a);
    code->and_(and_a_b, reg_b);
    code->xor_(xor_a_b, reg_b);
    code->mov(carry, xor_a_b);
    code->and_(carry, 0x80808080);
    code->shr(xor_a_b, 1);
    code->and_(xor_a_b, 0x7F7F7F7F);
    code->add(result, xor_a_b);
    code->xor_(result, carry);

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitPackedHalvingAddS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);
    Xbyak::Xmm tmp = reg_alloc.ScratchXmm();

    // This relies on the equality x+y == ((x&y) << 1) + (x^y).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x+y)/2, we can instead calculate (x&y) + ((x^y)>>>1).
    // The arithmetic shift right makes this signed.

    code->movdqa(tmp, xmm_a);
    code->pand(xmm_a, xmm_b);
    code->pxor(tmp, xmm_b);
    code->psraw(tmp, 1);
    code->paddw(xmm_a, tmp);

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedHalvingSubU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 minuend = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 subtrahend = reg_alloc.UseScratchGpr(args[1]).cvt32();

    // This relies on the equality x-y == (x^y) - (((x^y)&y) << 1).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x+y)/2, we can instead calculate ((x^y)>>1) - ((x^y)&y).

    code->xor_(minuend, subtrahend);
    code->and_(subtrahend, minuend);
    code->shr(minuend, 1);

    // At this point,
    // minuend := (a^b) >> 1
    // subtrahend := (a^b) & b

    // We must now perform a partitioned subtraction.
    // We can do this because minuend contains 7 bit fields.
    // We use the extra bit in minuend as a bit to borrow from; we set this bit.
    // We invert this bit at the end as this tells us if that bit was borrowed from.
    code->or_(minuend, 0x80808080);
    code->sub(minuend, subtrahend);
    code->xor_(minuend, 0x80808080);

    // minuend now contains the desired result.
    reg_alloc.DefineValue(inst, minuend);
}

void EmitX64::EmitPackedHalvingSubS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 minuend = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 subtrahend = reg_alloc.UseScratchGpr(args[1]).cvt32();

    Xbyak::Reg32 carry = reg_alloc.ScratchGpr().cvt32();

    // This relies on the equality x-y == (x^y) - (((x^y)&y) << 1).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x-y)/2, we can instead calculate ((x^y)>>1) - ((x^y)&y).

    code->xor_(minuend, subtrahend);
    code->and_(subtrahend, minuend);
    code->mov(carry, minuend);
    code->and_(carry, 0x80808080);
    code->shr(minuend, 1);

    // At this point,
    // minuend := (a^b) >> 1
    // subtrahend := (a^b) & b
    // carry := (a^b) & 0x80808080

    // We must now perform a partitioned subtraction.
    // We can do this because minuend contains 7 bit fields.
    // We use the extra bit in minuend as a bit to borrow from; we set this bit.
    // We invert this bit at the end as this tells us if that bit was borrowed from.
    // We then sign extend the result into this bit.
    code->or_(minuend, 0x80808080);
    code->sub(minuend, subtrahend);
    code->xor_(minuend, 0x80808080);
    code->xor_(minuend, carry);

    reg_alloc.DefineValue(inst, minuend);
}

void EmitX64::EmitPackedHalvingSubU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 minuend = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 subtrahend = reg_alloc.UseScratchGpr(args[1]).cvt32();

    // This relies on the equality x-y == (x^y) - (((x^y)&y) << 1).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x+y)/2, we can instead calculate ((x^y)>>1) - ((x^y)&y).

    code->xor_(minuend, subtrahend);
    code->and_(subtrahend, minuend);
    code->shr(minuend, 1);

    // At this point,
    // minuend := (a^b) >> 1
    // subtrahend := (a^b) & b

    // We must now perform a partitioned subtraction.
    // We can do this because minuend contains 15 bit fields.
    // We use the extra bit in minuend as a bit to borrow from; we set this bit.
    // We invert this bit at the end as this tells us if that bit was borrowed from.
    code->or_(minuend, 0x80008000);
    code->sub(minuend, subtrahend);
    code->xor_(minuend, 0x80008000);

    reg_alloc.DefineValue(inst, minuend);
}

void EmitX64::EmitPackedHalvingSubS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 minuend = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 subtrahend = reg_alloc.UseScratchGpr(args[1]).cvt32();

    Xbyak::Reg32 carry = reg_alloc.ScratchGpr().cvt32();

    // This relies on the equality x-y == (x^y) - (((x^y)&y) << 1).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x-y)/2, we can instead calculate ((x^y)>>1) - ((x^y)&y).

    code->xor_(minuend, subtrahend);
    code->and_(subtrahend, minuend);
    code->mov(carry, minuend);
    code->and_(carry, 0x80008000);
    code->shr(minuend, 1);

    // At this point,
    // minuend := (a^b) >> 1
    // subtrahend := (a^b) & b
    // carry := (a^b) & 0x80008000

    // We must now perform a partitioned subtraction.
    // We can do this because minuend contains 7 bit fields.
    // We use the extra bit in minuend as a bit to borrow from; we set this bit.
    // We invert this bit at the end as this tells us if that bit was borrowed from.
    // We then sign extend the result into this bit.
    code->or_(minuend, 0x80008000);
    code->sub(minuend, subtrahend);
    code->xor_(minuend, 0x80008000);
    code->xor_(minuend, carry);

    reg_alloc.DefineValue(inst, minuend);
}

void EmitPackedSubAdd(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, bool hi_is_sum, bool is_signed, bool is_halving) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto ge_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetGEFromOp);

    Xbyak::Reg32 reg_a_hi = reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg32 reg_b_hi = reg_alloc.UseScratchGpr(args[1]).cvt32();
    Xbyak::Reg32 reg_a_lo = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 reg_b_lo = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 reg_sum, reg_diff;

    if (is_signed) {
        code->movsx(reg_a_lo, reg_a_hi.cvt16());
        code->movsx(reg_b_lo, reg_b_hi.cvt16());
        code->sar(reg_a_hi, 16);
        code->sar(reg_b_hi, 16);
    } else {
        code->movzx(reg_a_lo, reg_a_hi.cvt16());
        code->movzx(reg_b_lo, reg_b_hi.cvt16());
        code->shr(reg_a_hi, 16);
        code->shr(reg_b_hi, 16);
    }

    if (hi_is_sum) {
        code->sub(reg_a_lo, reg_b_hi);
        code->add(reg_a_hi, reg_b_lo);
        reg_diff = reg_a_lo;
        reg_sum = reg_a_hi;
    } else {
        code->add(reg_a_lo, reg_b_hi);
        code->sub(reg_a_hi, reg_b_lo);
        reg_diff = reg_a_hi;
        reg_sum = reg_a_lo;
    }

    if (ge_inst) {
        EraseInstruction(block, ge_inst);

        // The reg_b registers are no longer required.
        Xbyak::Reg32 ge_sum = reg_b_hi;
        Xbyak::Reg32 ge_diff = reg_b_lo;

        code->mov(ge_sum, reg_sum);
        code->mov(ge_diff, reg_diff);

        if (!is_signed) {
            code->shl(ge_sum, 15);
            code->sar(ge_sum, 31);
        } else {
            code->not_(ge_sum);
            code->sar(ge_sum, 31);
        }
        code->not_(ge_diff);
        code->sar(ge_diff, 31);
        code->and_(ge_sum, hi_is_sum ? 0xFFFF0000 : 0x0000FFFF);
        code->and_(ge_diff, hi_is_sum ? 0x0000FFFF : 0xFFFF0000);
        code->or_(ge_sum, ge_diff);

        reg_alloc.DefineValue(ge_inst, ge_sum);
    }

    if (is_halving) {
        code->shl(reg_a_lo, 15);
        code->shr(reg_a_hi, 1);
    } else {
        code->shl(reg_a_lo, 16);
    }

    // reg_a_lo now contains the low word and reg_a_hi now contains the high word.
    // Merge them.
    code->shld(reg_a_hi, reg_a_lo, 16);

    reg_alloc.DefineValue(inst, reg_a_hi);
}

void EmitX64::EmitPackedAddSubU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, false, false);
}

void EmitX64::EmitPackedAddSubS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, true, false);
}

void EmitX64::EmitPackedSubAddU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, false, false);
}

void EmitX64::EmitPackedSubAddS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, true, false);
}

void EmitX64::EmitPackedHalvingAddSubU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, false, true);
}

void EmitX64::EmitPackedHalvingAddSubS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, true, true);
}

void EmitX64::EmitPackedHalvingSubAddU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, false, true);
}

void EmitX64::EmitPackedHalvingSubAddS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, true, true);
}

static void EmitPackedOperation(BlockOfCode* code, RegAlloc& reg_alloc, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Mmx& mmx, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    (code->*fn)(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}

void EmitX64::EmitPackedSaturatedAddU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddusb);
}

void EmitX64::EmitPackedSaturatedAddS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddsb);
}

void EmitX64::EmitPackedSaturatedSubU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubusb);
}

void EmitX64::EmitPackedSaturatedSubS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubsb);
}

void EmitX64::EmitPackedSaturatedAddU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddusw);
}

void EmitX64::EmitPackedSaturatedAddS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddsw);
}

void EmitX64::EmitPackedSaturatedSubU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubusw);
}

void EmitX64::EmitPackedSaturatedSubS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubsw);
}

void EmitX64::EmitPackedAbsDiffSumS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psadbw);
}

void EmitX64::EmitPackedSelect(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    size_t num_args_in_xmm = args[0].IsInXmm() + args[1].IsInXmm() + args[2].IsInXmm();

    if (num_args_in_xmm >= 2) {
        Xbyak::Xmm ge = reg_alloc.UseScratchXmm(args[0]);
        Xbyak::Xmm to = reg_alloc.UseXmm(args[1]);
        Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[2]);

        code->pand(from, ge);
        code->pandn(ge, to);
        code->por(from, ge);

        reg_alloc.DefineValue(inst, from);
    } else if (code->DoesCpuSupport(Xbyak::util::Cpu::tBMI1)) {
        Xbyak::Reg32 ge = reg_alloc.UseGpr(args[0]).cvt32();
        Xbyak::Reg32 to = reg_alloc.UseScratchGpr(args[1]).cvt32();
        Xbyak::Reg32 from = reg_alloc.UseScratchGpr(args[2]).cvt32();

        code->and_(from, ge);
        code->andn(to, ge, to);
        code->or_(from, to);

        reg_alloc.DefineValue(inst, from);
    } else {
        Xbyak::Reg32 ge = reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 to = reg_alloc.UseGpr(args[1]).cvt32();
        Xbyak::Reg32 from = reg_alloc.UseScratchGpr(args[2]).cvt32();

        code->and_(from, ge);
        code->not_(ge);
        code->and_(ge, to);
        code->or_(from, ge);

        reg_alloc.DefineValue(inst, from);
    }
}

static void DenormalsAreZero32(BlockOfCode* code, Xbyak::Xmm xmm_value, Xbyak::Reg32 gpr_scratch) {
    Xbyak::Label end;

    // We need to report back whether we've found a denormal on input.
    // SSE doesn't do this for us when SSE's DAZ is enabled.

    code->movd(gpr_scratch, xmm_value);
    code->and_(gpr_scratch, u32(0x7FFFFFFF));
    code->sub(gpr_scratch, u32(1));
    code->cmp(gpr_scratch, u32(0x007FFFFE));
    code->ja(end);
    code->pxor(xmm_value, xmm_value);
    code->mov(dword[r15 + offsetof(JitState, FPSCR_IDC)], u32(1 << 7));
    code->L(end);
}

static void DenormalsAreZero64(BlockOfCode* code, Xbyak::Xmm xmm_value, Xbyak::Reg64 gpr_scratch) {
    Xbyak::Label end;

    auto mask = code->MConst(f64_non_sign_mask);
    mask.setBit(64);
    auto penult_denormal = code->MConst(f64_penultimate_positive_denormal);
    penult_denormal.setBit(64);

    code->movq(gpr_scratch, xmm_value);
    code->and_(gpr_scratch, mask);
    code->sub(gpr_scratch, u32(1));
    code->cmp(gpr_scratch, penult_denormal);
    code->ja(end);
    code->pxor(xmm_value, xmm_value);
    code->mov(dword[r15 + offsetof(JitState, FPSCR_IDC)], u32(1 << 7));
    code->L(end);
}

static void FlushToZero32(BlockOfCode* code, Xbyak::Xmm xmm_value, Xbyak::Reg32 gpr_scratch) {
    Xbyak::Label end;

    code->movd(gpr_scratch, xmm_value);
    code->and_(gpr_scratch, u32(0x7FFFFFFF));
    code->sub(gpr_scratch, u32(1));
    code->cmp(gpr_scratch, u32(0x007FFFFE));
    code->ja(end);
    code->pxor(xmm_value, xmm_value);
    code->mov(dword[r15 + offsetof(JitState, FPSCR_UFC)], u32(1 << 3));
    code->L(end);
}

static void FlushToZero64(BlockOfCode* code, Xbyak::Xmm xmm_value, Xbyak::Reg64 gpr_scratch) {
    Xbyak::Label end;

    auto mask = code->MConst(f64_non_sign_mask);
    mask.setBit(64);
    auto penult_denormal = code->MConst(f64_penultimate_positive_denormal);
    penult_denormal.setBit(64);

    code->movq(gpr_scratch, xmm_value);
    code->and_(gpr_scratch, mask);
    code->sub(gpr_scratch, u32(1));
    code->cmp(gpr_scratch, penult_denormal);
    code->ja(end);
    code->pxor(xmm_value, xmm_value);
    code->mov(dword[r15 + offsetof(JitState, FPSCR_UFC)], u32(1 << 3));
    code->L(end);
}

static void DefaultNaN32(BlockOfCode* code, Xbyak::Xmm xmm_value) {
    Xbyak::Label end;

    code->ucomiss(xmm_value, xmm_value);
    code->jnp(end);
    code->movaps(xmm_value, code->MConst(f32_nan));
    code->L(end);
}

static void DefaultNaN64(BlockOfCode* code, Xbyak::Xmm xmm_value) {
    Xbyak::Label end;

    code->ucomisd(xmm_value, xmm_value);
    code->jnp(end);
    code->movaps(xmm_value, code->MConst(f64_nan));
    code->L(end);
}

static void ZeroIfNaN64(BlockOfCode* code, Xbyak::Xmm xmm_value, Xbyak::Xmm xmm_scratch) {
    code->pxor(xmm_scratch, xmm_scratch);
    code->cmpordsd(xmm_scratch, xmm_value); // true mask when ordered (i.e.: when not an NaN)
    code->pand(xmm_value, xmm_scratch);
}

static void FPThreeOp32(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand = reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Reg32 gpr_scratch = reg_alloc.ScratchGpr().cvt32();

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
        DenormalsAreZero32(code, operand, gpr_scratch);
    }
    (code->*fn)(result, operand);
    if (block.Location().FPSCR().FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (block.Location().FPSCR().DN()) {
        DefaultNaN32(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

static void FPThreeOp64(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand = reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
        DenormalsAreZero64(code, operand, gpr_scratch);
    }
    (code->*fn)(result, operand);
    if (block.Location().FPSCR().FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (block.Location().FPSCR().DN()) {
        DefaultNaN64(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

static void FPTwoOp32(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 gpr_scratch = reg_alloc.ScratchGpr().cvt32();

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
    }

    (code->*fn)(result, result);
    if (block.Location().FPSCR().FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (block.Location().FPSCR().DN()) {
        DefaultNaN32(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

static void FPTwoOp64(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
    }

    (code->*fn)(result, result);
    if (block.Location().FPSCR().FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (block.Location().FPSCR().DN()) {
        DefaultNaN64(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitTransferFromFP32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

void EmitX64::EmitTransferFromFP64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

void EmitX64::EmitTransferToFP32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate() && args[0].GetImmediateU32() == 0) {
        Xbyak::Xmm result = reg_alloc.ScratchXmm();
        code->xorps(result, result);
        reg_alloc.DefineValue(inst, result);
    } else {
        reg_alloc.DefineValue(inst, args[0]);
    }
}

void EmitX64::EmitTransferToFP64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate() && args[0].GetImmediateU64() == 0) {
        Xbyak::Xmm result = reg_alloc.ScratchXmm();
        code->xorps(result, result);
        reg_alloc.DefineValue(inst, result);
    } else {
        reg_alloc.DefineValue(inst, args[0]);
    }
}

void EmitX64::EmitFPAbs32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pand(result, code->MConst(f32_non_sign_mask));

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPAbs64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pand(result, code->MConst(f64_non_sign_mask));

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPNeg32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pxor(result, code->MConst(f32_negative_zero));

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPNeg64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pxor(result, code->MConst(f64_negative_zero));

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPAdd32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::addss);
}

void EmitX64::EmitFPAdd64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::addsd);
}

void EmitX64::EmitFPDiv32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::divss);
}

void EmitX64::EmitFPDiv64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::divsd);
}

void EmitX64::EmitFPMul32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::mulss);
}

void EmitX64::EmitFPMul64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::mulsd);
}

void EmitX64::EmitFPSqrt32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPTwoOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::sqrtss);
}

void EmitX64::EmitFPSqrt64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPTwoOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::sqrtsd);
}

void EmitX64::EmitFPSub32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::subss);
}

void EmitX64::EmitFPSub64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::subsd);
}

static void SetFpscrNzcvFromFlags(BlockOfCode* code, RegAlloc& reg_alloc) {
    reg_alloc.ScratchGpr({HostLoc::RCX}); // shifting requires use of cl
    Xbyak::Reg32 nzcv = reg_alloc.ScratchGpr().cvt32();


    code->mov(nzcv, 0x28630000);
    code->sete(cl);
    code->rcl(cl, 3);
    code->shl(nzcv, cl);
    code->and_(nzcv, 0xF0000000);
    code->mov(dword[r15 + offsetof(JitState, FPSCR_nzcv)], nzcv);
}

void EmitX64::EmitFPCompare32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm reg_a = reg_alloc.UseXmm(args[0]);
    Xbyak::Xmm reg_b = reg_alloc.UseXmm(args[1]);
    bool exc_on_qnan = args[2].GetImmediateU1();

    if (exc_on_qnan) {
        code->comiss(reg_a, reg_b);
    } else {
        code->ucomiss(reg_a, reg_b);
    }

    SetFpscrNzcvFromFlags(code, reg_alloc);
}

void EmitX64::EmitFPCompare64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm reg_a = reg_alloc.UseXmm(args[0]);
    Xbyak::Xmm reg_b = reg_alloc.UseXmm(args[1]);
    bool exc_on_qnan = args[2].GetImmediateU1();

    if (exc_on_qnan) {
        code->comisd(reg_a, reg_b);
    } else {
        code->ucomisd(reg_a, reg_b);
    }

    SetFpscrNzcvFromFlags(code, reg_alloc);
}

void EmitX64::EmitFPSingleToDouble(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch.cvt32());
    }
    code->cvtss2sd(result, result);
    if (block.Location().FPSCR().FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (block.Location().FPSCR().DN()) {
        DefaultNaN64(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPDoubleToSingle(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
    }
    code->cvtsd2ss(result, result);
    if (block.Location().FPSCR().FTZ()) {
        FlushToZero32(code, result, gpr_scratch.cvt32());
    }
    if (block.Location().FPSCR().DN()) {
        DefaultNaN32(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitFPSingleToS32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = reg_alloc.ScratchXmm();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // Conversion to double is lossless, and allows for clamping.

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero32(code, from, to);
    }
    code->cvtss2sd(from, from);
    // First time is to set flags
    if (round_towards_zero) {
        code->cvttsd2si(to, from); // 32 bit gpr
    } else {
        code->cvtsd2si(to, from); // 32 bit gpr
    }
    // Clamp to output range
    ZeroIfNaN64(code, from, xmm_scratch);
    code->minsd(from, code->MConst(f64_max_s32));
    code->maxsd(from, code->MConst(f64_min_s32));
    // Second time is for real
    if (round_towards_zero) {
        code->cvttsd2si(to, from); // 32 bit gpr
    } else {
        code->cvtsd2si(to, from); // 32 bit gpr
    }

    reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPSingleToU32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = reg_alloc.ScratchXmm();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // Conversion to double is lossless, and allows for accurate clamping.
    //
    // Since SSE2 doesn't provide an unsigned conversion, we shift the range as appropriate.
    //
    // FIXME: Inexact exception not correctly signalled with the below code

    if (block.Location().FPSCR().RMode() != Arm::FPSCR::RoundingMode::TowardsZero && !round_towards_zero) {
        if (block.Location().FPSCR().FTZ()) {
            DenormalsAreZero32(code, from, to);
        }
        code->cvtss2sd(from, from);
        ZeroIfNaN64(code, from, xmm_scratch);
        // Bring into SSE range
        code->addsd(from, code->MConst(f64_min_s32));
        // First time is to set flags
        code->cvtsd2si(to, from); // 32 bit gpr
        // Clamp to output range
        code->minsd(from, code->MConst(f64_max_s32));
        code->maxsd(from, code->MConst(f64_min_s32));
        // Actually convert
        code->cvtsd2si(to, from); // 32 bit gpr
        // Bring back into original range
        code->add(to, u32(2147483648u));
    } else {
        Xbyak::Xmm xmm_mask = reg_alloc.ScratchXmm();
        Xbyak::Reg32 gpr_mask = reg_alloc.ScratchGpr().cvt32();

        if (block.Location().FPSCR().FTZ()) {
            DenormalsAreZero32(code, from, to);
        }
        code->cvtss2sd(from, from);
        ZeroIfNaN64(code, from, xmm_scratch);
        // Generate masks if out-of-signed-range
        code->movaps(xmm_mask, code->MConst(f64_max_s32));
        code->cmpltsd(xmm_mask, from);
        code->movd(gpr_mask, xmm_mask);
        code->pand(xmm_mask, code->MConst(f64_min_s32));
        code->and_(gpr_mask, u32(2147483648u));
        // Bring into range if necessary
        code->addsd(from, xmm_mask);
        // First time is to set flags
        code->cvttsd2si(to, from); // 32 bit gpr
        // Clamp to output range
        code->minsd(from, code->MConst(f64_max_s32));
        code->maxsd(from, code->MConst(f64_min_u32));
        // Actually convert
        code->cvttsd2si(to, from); // 32 bit gpr
        // Bring back into original range if necessary
        code->add(to, gpr_mask);
    }

    reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPDoubleToS32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = reg_alloc.ScratchXmm();
    Xbyak::Reg32 gpr_scratch = reg_alloc.ScratchGpr().cvt32();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.

    if (block.Location().FPSCR().FTZ()) {
        DenormalsAreZero64(code, from, gpr_scratch.cvt64());
    }
    // First time is to set flags
    if (round_towards_zero) {
        code->cvttsd2si(gpr_scratch, from); // 32 bit gpr
    } else {
        code->cvtsd2si(gpr_scratch, from); // 32 bit gpr
    }
    // Clamp to output range
    ZeroIfNaN64(code, from, xmm_scratch);
    code->minsd(from, code->MConst(f64_max_s32));
    code->maxsd(from, code->MConst(f64_min_s32));
    // Second time is for real
    if (round_towards_zero) {
        code->cvttsd2si(to, from); // 32 bit gpr
    } else {
        code->cvtsd2si(to, from); // 32 bit gpr
    }

    reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPDoubleToU32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = reg_alloc.ScratchXmm();
    Xbyak::Reg32 gpr_scratch = reg_alloc.ScratchGpr().cvt32();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // TODO: Use VCVTPD2UDQ when AVX512VL is available.
    // FIXME: Inexact exception not correctly signalled with the below code

    if (block.Location().FPSCR().RMode() != Arm::FPSCR::RoundingMode::TowardsZero && !round_towards_zero) {
        if (block.Location().FPSCR().FTZ()) {
            DenormalsAreZero64(code, from, gpr_scratch.cvt64());
        }
        ZeroIfNaN64(code, from, xmm_scratch);
        // Bring into SSE range
        code->addsd(from, code->MConst(f64_min_s32));
        // First time is to set flags
        code->cvtsd2si(gpr_scratch, from); // 32 bit gpr
        // Clamp to output range
        code->minsd(from, code->MConst(f64_max_s32));
        code->maxsd(from, code->MConst(f64_min_s32));
        // Actually convert
        code->cvtsd2si(to, from); // 32 bit gpr
        // Bring back into original range
        code->add(to, u32(2147483648u));
    } else {
        Xbyak::Xmm xmm_mask = reg_alloc.ScratchXmm();
        Xbyak::Reg32 gpr_mask = reg_alloc.ScratchGpr().cvt32();

        if (block.Location().FPSCR().FTZ()) {
            DenormalsAreZero64(code, from, gpr_scratch.cvt64());
        }
        ZeroIfNaN64(code, from, xmm_scratch);
        // Generate masks if out-of-signed-range
        code->movaps(xmm_mask, code->MConst(f64_max_s32));
        code->cmpltsd(xmm_mask, from);
        code->movd(gpr_mask, xmm_mask);
        code->pand(xmm_mask, code->MConst(f64_min_s32));
        code->and_(gpr_mask, u32(2147483648u));
        // Bring into range if necessary
        code->addsd(from, xmm_mask);
        // First time is to set flags
        code->cvttsd2si(gpr_scratch, from); // 32 bit gpr
        // Clamp to output range
        code->minsd(from, code->MConst(f64_max_s32));
        code->maxsd(from, code->MConst(f64_min_u32));
        // Actually convert
        code->cvttsd2si(to, from); // 32 bit gpr
        // Bring back into original range if necessary
        code->add(to, gpr_mask);
    }

    reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPS32ToSingle(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 from = reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Xmm to = reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code->cvtsi2ss(to, from);

    reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPU32ToSingle(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 from = reg_alloc.UseGpr(args[0]);
    Xbyak::Xmm to = reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    // We are using a 64-bit GPR register to ensure we don't end up treating the input as signed
    code->mov(from.cvt32(), from.cvt32()); // TODO: Verify if this is necessary
    code->cvtsi2ss(to, from);

    reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPS32ToDouble(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 from = reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Xmm to = reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code->cvtsi2sd(to, from);

    reg_alloc.DefineValue(inst, to);
}

void EmitX64::EmitFPU32ToDouble(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 from = reg_alloc.UseGpr(args[0]);
    Xbyak::Xmm to = reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    // We are using a 64-bit GPR register to ensure we don't end up treating the input as signed
    code->mov(from.cvt32(), from.cvt32()); // TODO: Verify if this is necessary
    code->cvtsi2sd(to, from);

    reg_alloc.DefineValue(inst, to);
}


void EmitX64::EmitClearExclusive(RegAlloc&, IR::Block&, IR::Inst*) {
    code->mov(code->byte[r15 + offsetof(JitState, exclusive_state)], u8(0));
}

void EmitX64::EmitSetExclusive(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitReadMemory8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 8, cb.memory.Read8);
}

void EmitX64::EmitReadMemory16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 16, cb.memory.Read16);
}

void EmitX64::EmitReadMemory32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 32, cb.memory.Read32);
}

void EmitX64::EmitReadMemory64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ReadMemory(code, reg_alloc, inst, cb, 64, cb.memory.Read64);
}

void EmitX64::EmitWriteMemory8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    WriteMemory(code, reg_alloc, inst, cb, 8, cb.memory.Write8);
}

void EmitX64::EmitWriteMemory16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    WriteMemory(code, reg_alloc, inst, cb, 16, cb.memory.Write16);
}

void EmitX64::EmitWriteMemory32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    WriteMemory(code, reg_alloc, inst, cb, 32, cb.memory.Write32);
}

void EmitX64::EmitWriteMemory64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitExclusiveWriteMemory8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ExclusiveWrite(code, reg_alloc, inst, cb.memory.Write8, false);
}

void EmitX64::EmitExclusiveWriteMemory16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ExclusiveWrite(code, reg_alloc, inst, cb.memory.Write16, false);
}

void EmitX64::EmitExclusiveWriteMemory32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    ExclusiveWrite(code, reg_alloc, inst, cb.memory.Write32, false);
}

void EmitX64::EmitExclusiveWriteMemory64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

void EmitX64::EmitCoprocInternalOperation(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    Arm::CoprocReg CRd = static_cast<Arm::CoprocReg>(coproc_info[3]);
    Arm::CoprocReg CRn = static_cast<Arm::CoprocReg>(coproc_info[4]);
    Arm::CoprocReg CRm = static_cast<Arm::CoprocReg>(coproc_info[5]);
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

void EmitX64::EmitCoprocSendOneWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    Arm::CoprocReg CRn = static_cast<Arm::CoprocReg>(coproc_info[3]);
    Arm::CoprocReg CRm = static_cast<Arm::CoprocReg>(coproc_info[4]);
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

void EmitX64::EmitCoprocSendTwoWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc = static_cast<unsigned>(coproc_info[2]);
    Arm::CoprocReg CRm = static_cast<Arm::CoprocReg>(coproc_info[3]);

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

void EmitX64::EmitCoprocGetOneWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc1 = static_cast<unsigned>(coproc_info[2]);
    Arm::CoprocReg CRn = static_cast<Arm::CoprocReg>(coproc_info[3]);
    Arm::CoprocReg CRm = static_cast<Arm::CoprocReg>(coproc_info[4]);
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

void EmitX64::EmitCoprocGetTwoWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    unsigned opc = coproc_info[2];
    Arm::CoprocReg CRm = static_cast<Arm::CoprocReg>(coproc_info[3]);

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

void EmitX64::EmitCoprocLoadWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    bool long_transfer = coproc_info[2] != 0;
    Arm::CoprocReg CRd = static_cast<Arm::CoprocReg>(coproc_info[3]);
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

void EmitX64::EmitCoprocStoreWords(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    auto coproc_info = inst->GetArg(0).GetCoprocInfo();

    size_t coproc_num = coproc_info[0];
    bool two = coproc_info[1] != 0;
    bool long_transfer = coproc_info[2] != 0;
    Arm::CoprocReg CRd = static_cast<Arm::CoprocReg>(coproc_info[3]);
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

void EmitX64::EmitAddCycles(size_t cycles) {
    ASSERT(cycles < std::numeric_limits<u32>::max());
    code->sub(qword[r15 + offsetof(JitState, cycles_remaining)], static_cast<u32>(cycles));
}

static Xbyak::Label EmitCond(BlockOfCode* code, Arm::Cond cond) {
    Xbyak::Label label;

    const Xbyak::Reg32 cpsr = eax;
    code->mov(cpsr, dword[r15 + offsetof(JitState, CPSR_nzcv)]);

    constexpr size_t n_shift = 31;
    constexpr size_t z_shift = 30;
    constexpr size_t c_shift = 29;
    constexpr size_t v_shift = 28;
    constexpr u32 n_mask = 1u << n_shift;
    constexpr u32 z_mask = 1u << z_shift;
    constexpr u32 c_mask = 1u << c_shift;
    constexpr u32 v_mask = 1u << v_shift;

    switch (cond) {
    case Arm::Cond::EQ: //z
        code->test(cpsr, z_mask);
        code->jnz(label);
        break;
    case Arm::Cond::NE: //!z
        code->test(cpsr, z_mask);
        code->jz(label);
        break;
    case Arm::Cond::CS: //c
        code->test(cpsr, c_mask);
        code->jnz(label);
        break;
    case Arm::Cond::CC: //!c
        code->test(cpsr, c_mask);
        code->jz(label);
        break;
    case Arm::Cond::MI: //n
        code->test(cpsr, n_mask);
        code->jnz(label);
        break;
    case Arm::Cond::PL: //!n
        code->test(cpsr, n_mask);
        code->jz(label);
        break;
    case Arm::Cond::VS: //v
        code->test(cpsr, v_mask);
        code->jnz(label);
        break;
    case Arm::Cond::VC: //!v
        code->test(cpsr, v_mask);
        code->jz(label);
        break;
    case Arm::Cond::HI: { //c & !z
        code->and_(cpsr, z_mask | c_mask);
        code->cmp(cpsr, c_mask);
        code->je(label);
        break;
    }
    case Arm::Cond::LS: { //!c | z
        code->and_(cpsr, z_mask | c_mask);
        code->cmp(cpsr, c_mask);
        code->jne(label);
        break;
    }
    case Arm::Cond::GE: { // n == v
        code->and_(cpsr, n_mask | v_mask);
        code->jz(label);
        code->cmp(cpsr, n_mask | v_mask);
        code->je(label);
        break;
    }
    case Arm::Cond::LT: { // n != v
        Xbyak::Label fail;
        code->and_(cpsr, n_mask | v_mask);
        code->jz(fail);
        code->cmp(cpsr, n_mask | v_mask);
        code->jne(label);
        code->L(fail);
        break;
    }
    case Arm::Cond::GT: { // !z & (n == v)
        const Xbyak::Reg32 tmp1 = ebx;
        const Xbyak::Reg32 tmp2 = esi;
        code->mov(tmp1, cpsr);
        code->mov(tmp2, cpsr);
        code->shr(tmp1, n_shift);
        code->shr(tmp2, v_shift);
        code->shr(cpsr, z_shift);
        code->xor_(tmp1, tmp2);
        code->or_(tmp1, cpsr);
        code->test(tmp1, 1);
        code->jz(label);
        break;
    }
    case Arm::Cond::LE: { // z | (n != v)
        const Xbyak::Reg32 tmp1 = ebx;
        const Xbyak::Reg32 tmp2 = esi;
        code->mov(tmp1, cpsr);
        code->mov(tmp2, cpsr);
        code->shr(tmp1, n_shift);
        code->shr(tmp2, v_shift);
        code->shr(cpsr, z_shift);
        code->xor_(tmp1, tmp2);
        code->or_(tmp1, cpsr);
        code->test(tmp1, 1);
        code->jnz(label);
        break;
    }
    default:
        ASSERT_MSG(false, "Unknown cond %zu", static_cast<size_t>(cond));
        break;
    }

    return label;
}

void EmitX64::EmitCondPrelude(const IR::Block& block) {
    if (block.GetCondition() == Arm::Cond::AL) {
        ASSERT(!block.HasConditionFailedLocation());
        return;
    }

    ASSERT(block.HasConditionFailedLocation());

    Xbyak::Label pass = EmitCond(code, block.GetCondition());
    EmitAddCycles(block.ConditionFailedCycleCount());
    EmitTerminal(IR::Term::LinkBlock{block.ConditionFailedLocation()}, block.Location());
    code->L(pass);
}

void EmitX64::EmitTerminal(IR::Terminal terminal, IR::LocationDescriptor initial_location) {
    Common::VisitVariant<void>(terminal, [this, &initial_location](auto x) {
        this->EmitTerminal(x, initial_location);
    });
}

void EmitX64::EmitTerminal(IR::Term::Interpret terminal, IR::LocationDescriptor initial_location) {
    ASSERT_MSG(terminal.next.TFlag() == initial_location.TFlag(), "Unimplemented");
    ASSERT_MSG(terminal.next.EFlag() == initial_location.EFlag(), "Unimplemented");

    code->mov(code->ABI_PARAM1.cvt32(), terminal.next.PC());
    code->mov(code->ABI_PARAM2, reinterpret_cast<u64>(jit_interface));
    code->mov(code->ABI_PARAM3, reinterpret_cast<u64>(cb.user_arg));
    code->mov(MJitStateReg(Arm::Reg::PC), code->ABI_PARAM1.cvt32());
    code->SwitchMxcsrOnExit();
    code->CallFunction(cb.InterpreterFallback);
    code->ReturnFromRunCode(true); // TODO: Check cycles
}

void EmitX64::EmitTerminal(IR::Term::ReturnToDispatch, IR::LocationDescriptor) {
    code->ReturnFromRunCode();
}

static u32 CalculateCpsr_et(const IR::LocationDescriptor& desc) {
    u32 et = 0;
    et |= desc.EFlag() ? 2 : 0;
    et |= desc.TFlag() ? 1 : 0;
    return et;
}

void EmitX64::EmitTerminal(IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location) {
    if (CalculateCpsr_et(terminal.next) != CalculateCpsr_et(initial_location)) {
        code->mov(dword[r15 + offsetof(JitState, CPSR_et)], CalculateCpsr_et(terminal.next));
    }

    code->cmp(qword[r15 + offsetof(JitState, cycles_remaining)], 0);

    patch_information[terminal.next.UniqueHash()].jg.emplace_back(code->getCurr());
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
    code->mov(MJitStateReg(Arm::Reg::PC), terminal.next.PC());
    PushRSBHelper(rax, rbx, terminal.next.UniqueHash());
    code->ForceReturnFromRunCode();
    code->SwitchToNearCode();
}

void EmitX64::EmitTerminal(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location) {
    if (CalculateCpsr_et(terminal.next) != CalculateCpsr_et(initial_location)) {
        code->mov(dword[r15 + offsetof(JitState, CPSR_et)], CalculateCpsr_et(terminal.next));
    }

    patch_information[terminal.next.UniqueHash()].jmp.emplace_back(code->getCurr());
    if (auto next_bb = GetBasicBlock(terminal.next)) {
        EmitPatchJmp(terminal.next, next_bb->entrypoint);
    } else {
        EmitPatchJmp(terminal.next);
    }
}

void EmitX64::EmitTerminal(IR::Term::PopRSBHint, IR::LocationDescriptor) {
    // This calculation has to match up with IREmitter::PushRSB
    // TODO: Optimization is available here based on known state of FPSCR_mode and CPSR_et.
    code->mov(ecx, MJitStateReg(Arm::Reg::PC));
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

void EmitX64::EmitTerminal(IR::Term::If terminal, IR::LocationDescriptor initial_location) {
    Xbyak::Label pass = EmitCond(code, terminal.if_);
    EmitTerminal(terminal.else_, initial_location);
    code->L(pass);
    EmitTerminal(terminal.then_, initial_location);
}

void EmitX64::EmitTerminal(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location) {
    code->cmp(code->byte[r15 + offsetof(JitState, halt_requested)], u8(0));
    code->jne(code->GetForceReturnFromRunCodeAddress());
    EmitTerminal(terminal.else_, initial_location);
}

void EmitX64::Patch(const IR::LocationDescriptor& desc, CodePtr bb) {
    const CodePtr save_code_ptr = code->getCurr();
    const PatchInformation& patch_info = patch_information[desc.UniqueHash()];

    for (CodePtr location : patch_info.jg) {
        code->SetCodePtr(location);
        EmitPatchJg(desc, bb);
    }

    for (CodePtr location : patch_info.jmp) {
        code->SetCodePtr(location);
        EmitPatchJmp(desc, bb);
    }

    for (CodePtr location : patch_info.mov_rcx) {
        code->SetCodePtr(location);
        EmitPatchMovRcx(bb);
    }

    code->SetCodePtr(save_code_ptr);
}

void EmitX64::Unpatch(const IR::LocationDescriptor& desc) {
    Patch(desc, nullptr);
}

void EmitX64::EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code->getCurr();
    if (target_code_ptr) {
        code->jg(target_code_ptr);
    } else {
        code->mov(MJitStateReg(Arm::Reg::PC), target_desc.PC());
        code->jg(code->GetReturnFromRunCodeAddress());
    }
    code->EnsurePatchLocationSize(patch_location, 14);
}

void EmitX64::EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr patch_location = code->getCurr();
    if (target_code_ptr) {
        code->jmp(target_code_ptr);
    } else {
        code->mov(MJitStateReg(Arm::Reg::PC), target_desc.PC());
        code->jmp(code->GetReturnFromRunCodeAddress());
    }
    code->EnsurePatchLocationSize(patch_location, 13);
}

void EmitX64::EmitPatchMovRcx(CodePtr target_code_ptr) {
    if (!target_code_ptr) {
        target_code_ptr = code->GetReturnFromRunCodeAddress();
    }
    const CodePtr patch_location = code->getCurr();
    code->mov(code->rcx, reinterpret_cast<u64>(target_code_ptr));
    code->EnsurePatchLocationSize(patch_location, 10);
}

void EmitX64::ClearCache() {
    block_descriptors.clear();
    patch_information.clear();
}

void EmitX64::InvalidateCacheRanges(const boost::icl::interval_set<u32>& ranges) {
    // Remove cached block descriptors and patch information overlapping with the given range.
    for (auto invalidate_interval : ranges) {
        auto pair = block_ranges.equal_range(invalidate_interval);
        for (auto it = pair.first; it != pair.second; ++it) {
            for (const auto& descriptor : it->second) {
                if (patch_information.count(descriptor.UniqueHash())) {
                    Unpatch(descriptor);
                }
                block_descriptors.erase(descriptor.UniqueHash());
            }
        }
        block_ranges.erase(pair.first, pair.second);
    }
}

} // namespace BackendX64
} // namespace Dynarmic
