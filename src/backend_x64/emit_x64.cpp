/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <unordered_map>
#include <unordered_set>

#include <dynarmic/coprocessor.h>

#include "backend_x64/abi.h"
#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/address_range.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/variant_util.h"
#include "frontend/ir/basic_block.h"
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

static void EraseInstruction(IR::Block& block, IR::Inst* inst) {
    block.Instructions().erase(inst);
    inst->Invalidate();
}

template <typename PCT>
EmitX64<PCT>::EmitX64(BlockOfCode* code, UserCallbacks cb, Jit* jit_interface)
    : code(code), cb(cb), jit_interface(jit_interface) {
}

template <typename PCT>
EmitX64<PCT>::~EmitX64() {}

template <typename PCT>
boost::optional<typename EmitX64<PCT>::BlockDescriptor> EmitX64<PCT>::GetBasicBlock(IR::LocationDescriptor descriptor) const {
    auto iter = block_descriptors.find(descriptor);
    if (iter == block_descriptors.end())
        return boost::none;
    return iter->second;
}

template <typename PCT>
void EmitX64<PCT>::EmitVoid(RegAlloc&, IR::Block&, IR::Inst*) {
}

template <typename PCT>
void EmitX64<PCT>::EmitBreakpoint(RegAlloc&, IR::Block&, IR::Inst*) {
    code->int3();
}

template <typename PCT>
void EmitX64<PCT>::EmitIdentity(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (!args[0].IsImmediate()) {
        reg_alloc.DefineValue(inst, args[0]);
    }
}

template <typename PCT>
void EmitX64<PCT>::PushRSBHelper(Xbyak::Reg64 loc_desc_reg, Xbyak::Reg64 index_reg, IR::LocationDescriptor target) {
    using namespace Xbyak::util;

    auto iter = block_descriptors.find(target);
    CodePtr target_code_ptr = iter != block_descriptors.end()
                            ? iter->second.entrypoint
                            : code->GetReturnFromRunCodeAddress();

    code->mov(index_reg.cvt32(), dword[r15 + offsetof(A32JitState, rsb_ptr)]);

    code->mov(loc_desc_reg, target.Value());

    patch_information[target].mov_rcx.emplace_back(code->getCurr());
    EmitPatchMovRcx(target_code_ptr);

    code->mov(qword[r15 + index_reg * 8 + offsetof(A32JitState, rsb_location_descriptors)], loc_desc_reg);
    code->mov(qword[r15 + index_reg * 8 + offsetof(A32JitState, rsb_codeptrs)], rcx);

    code->add(index_reg.cvt32(), 1);
    code->and_(index_reg.cvt32(), u32(A32JitState::RSBPtrMask));
    code->mov(dword[r15 + offsetof(A32JitState, rsb_ptr)], index_reg.cvt32());
}

template <typename PCT>
void EmitX64<PCT>::EmitPushRSB(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    u64 unique_hash_of_target = args[0].GetImmediateU64();

    reg_alloc.ScratchGpr({HostLoc::RCX});
    Xbyak::Reg64 loc_desc_reg = reg_alloc.ScratchGpr();
    Xbyak::Reg64 index_reg = reg_alloc.ScratchGpr();

    PushRSBHelper(loc_desc_reg, index_reg, IR::LocationDescriptor{unique_hash_of_target});
}

template <typename PCT>
void EmitX64<PCT>::EmitGetCarryFromOp(RegAlloc&, IR::Block&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

template <typename PCT>
void EmitX64<PCT>::EmitGetOverflowFromOp(RegAlloc&, IR::Block&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

template <typename PCT>
void EmitX64<PCT>::EmitGetGEFromOp(RegAlloc&, IR::Block&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

template <typename PCT>
void EmitX64<PCT>::EmitPack2x32To1x64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 lo = reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg64 hi = reg_alloc.UseScratchGpr(args[1]);

    code->shl(hi, 32);
    code->mov(lo.cvt32(), lo.cvt32()); // Zero extend to 64-bits
    code->or_(lo, hi);

    reg_alloc.DefineValue(inst, lo);
}

template <typename PCT>
void EmitX64<PCT>::EmitLeastSignificantWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

template <typename PCT>
void EmitX64<PCT>::EmitMostSignificantWord(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitLeastSignificantHalf(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

template <typename PCT>
void EmitX64<PCT>::EmitLeastSignificantByte(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

template <typename PCT>
void EmitX64<PCT>::EmitMostSignificantBit(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    // TODO: Flag optimization
    code->shr(result, 31);
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitIsZero(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    // TODO: Flag optimization
    code->test(result, result);
    code->sete(result.cvt8());
    code->movzx(result, result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitIsZero64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    // TODO: Flag optimization
    code->test(result, result);
    code->sete(result.cvt8());
    code->movzx(result, result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitLogicalShiftLeft(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitLogicalShiftRight(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitLogicalShiftRight64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitArithmeticShiftRight(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitRotateRight(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitRotateRightExtended(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitAddWithCarry(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitAdd64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg64 op_arg = reg_alloc.UseGpr(args[1]);

    code->add(result, op_arg);

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitSubWithCarry(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitSub64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg64 op_arg = reg_alloc.UseGpr(args[1]);

    code->sub(result, op_arg);

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitMul(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitMul64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    OpArg op_arg = reg_alloc.UseOpArg(args[1]);

    code->imul(result, *op_arg);

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitAnd(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitEor(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitOr(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitNot(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitSignExtendWordToLong(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movsxd(result.cvt64(), result.cvt32());
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitSignExtendHalfToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt32(), result.cvt16());
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitSignExtendByteToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt32(), result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitZeroExtendWordToLong(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->mov(result.cvt32(), result.cvt32()); // x64 zeros upper 32 bits on a 32-bit move
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitZeroExtendHalfToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt16());
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitZeroExtendByteToWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt8());
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitByteReverseWord(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = reg_alloc.UseScratchGpr(args[0]).cvt32();
    code->bswap(result);
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitByteReverseHalf(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg16 result = reg_alloc.UseScratchGpr(args[0]).cvt16();
    code->rol(result, 8);
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitByteReverseDual(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = reg_alloc.UseScratchGpr(args[0]);
    code->bswap(result);
    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitCountLeadingZeros(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitSignedSaturatedAdd(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitSignedSaturatedSub(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitUnsignedSaturation(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitSignedSaturation(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedAddU8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedAddS8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedAddU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedAddS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedSubU8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedSubS8(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedSubU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedSubS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingAddU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingAddU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingAddS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingAddS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingSubU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingSubS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingSubU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm minuend = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm subtrahend = reg_alloc.UseScratchXmm(args[1]);

    // This relies on the equality x-y == (x^y) - (((x^y)&y) << 1).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x-y)/2, we can instead calculate ((x^y)>>1) - ((x^y)&y).

    code->pxor(minuend, subtrahend);
    code->pand(subtrahend, minuend);
    code->psrlw(minuend, 1);

    // At this point,
    // minuend := (a^b) >> 1
    // subtrahend := (a^b) & b

    code->psubw(minuend, subtrahend);

    reg_alloc.DefineValue(inst, minuend);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingSubS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm minuend = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm subtrahend = reg_alloc.UseScratchXmm(args[1]);

    // This relies on the equality x-y == (x^y) - (((x^y)&y) << 1).
    // Note that x^y always contains the LSB of the result.
    // Since we want to calculate (x-y)/2, we can instead calculate ((x^y)>>>1) - ((x^y)&y).

    code->pxor(minuend, subtrahend);
    code->pand(subtrahend, minuend);
    code->psraw(minuend, 1);

    // At this point,
    // minuend := (a^b) >>> 1
    // subtrahend := (a^b) & b

    code->psubw(minuend, subtrahend);

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

template <typename PCT>
void EmitX64<PCT>::EmitPackedAddSubU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, false, false);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedAddSubS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, true, false);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSubAddU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, false, false);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSubAddS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, true, false);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingAddSubU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, false, true);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingAddSubS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, true, true, true);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingSubAddU16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, false, true);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedHalvingSubAddS16(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    EmitPackedSubAdd(code, reg_alloc, block, inst, false, true, true);
}

static void EmitPackedOperation(BlockOfCode* code, RegAlloc& reg_alloc, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Mmx& mmx, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = reg_alloc.UseXmm(args[1]);

    (code->*fn)(xmm_a, xmm_b);

    reg_alloc.DefineValue(inst, xmm_a);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedAddU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddusb);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedAddS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddsb);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedSubU8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubusb);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedSubS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubsb);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedAddU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddusw);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedAddS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::paddsw);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedSubU16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubusw);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSaturatedSubS16(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psubsw);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedAbsDiffSumS8(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    EmitPackedOperation(code, reg_alloc, inst, &Xbyak::CodeGenerator::psadbw);
}

template <typename PCT>
void EmitX64<PCT>::EmitPackedSelect(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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
    code->mov(dword[r15 + offsetof(A32JitState, FPSCR_IDC)], u32(1 << 7));
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
    code->mov(dword[r15 + offsetof(A32JitState, FPSCR_IDC)], u32(1 << 7));
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
    code->mov(dword[r15 + offsetof(A32JitState, FPSCR_UFC)], u32(1 << 3));
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
    code->mov(dword[r15 + offsetof(A32JitState, FPSCR_UFC)], u32(1 << 3));
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

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
        DenormalsAreZero32(code, operand, gpr_scratch);
    }
    (code->*fn)(result, operand);
    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (A32::LocationDescriptor{block.Location()}.FPSCR().DN()) {
        DefaultNaN32(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

static void FPThreeOp64(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm operand = reg_alloc.UseScratchXmm(args[1]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
        DenormalsAreZero64(code, operand, gpr_scratch);
    }
    (code->*fn)(result, operand);
    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (A32::LocationDescriptor{block.Location()}.FPSCR().DN()) {
        DefaultNaN64(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

static void FPTwoOp32(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 gpr_scratch = reg_alloc.ScratchGpr().cvt32();

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch);
    }

    (code->*fn)(result, result);
    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        FlushToZero32(code, result, gpr_scratch);
    }
    if (A32::LocationDescriptor{block.Location()}.FPSCR().DN()) {
        DefaultNaN32(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

static void FPTwoOp64(BlockOfCode* code, RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Xmm&, const Xbyak::Operand&)) {
    auto args = reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
    }

    (code->*fn)(result, result);
    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (A32::LocationDescriptor{block.Location()}.FPSCR().DN()) {
        DefaultNaN64(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitTransferFromFP32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

template <typename PCT>
void EmitX64<PCT>::EmitTransferFromFP64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    reg_alloc.DefineValue(inst, args[0]);
}

template <typename PCT>
void EmitX64<PCT>::EmitTransferToFP32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate() && args[0].GetImmediateU32() == 0) {
        Xbyak::Xmm result = reg_alloc.ScratchXmm();
        code->xorps(result, result);
        reg_alloc.DefineValue(inst, result);
    } else {
        reg_alloc.DefineValue(inst, args[0]);
    }
}

template <typename PCT>
void EmitX64<PCT>::EmitTransferToFP64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    if (args[0].IsImmediate() && args[0].GetImmediateU64() == 0) {
        Xbyak::Xmm result = reg_alloc.ScratchXmm();
        code->xorps(result, result);
        reg_alloc.DefineValue(inst, result);
    } else {
        reg_alloc.DefineValue(inst, args[0]);
    }
}

template <typename PCT>
void EmitX64<PCT>::EmitFPAbs32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pand(result, code->MConst(f32_non_sign_mask));

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPAbs64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pand(result, code->MConst(f64_non_sign_mask));

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPNeg32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pxor(result, code->MConst(f32_negative_zero));

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPNeg64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);

    code->pxor(result, code->MConst(f64_negative_zero));

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPAdd32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::addss);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPAdd64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::addsd);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPDiv32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::divss);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPDiv64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::divsd);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPMul32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::mulss);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPMul64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::mulsd);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPSqrt32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPTwoOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::sqrtss);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPSqrt64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPTwoOp64(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::sqrtsd);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPSub32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    FPThreeOp32(code, reg_alloc, block, inst, &Xbyak::CodeGenerator::subss);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPSub64(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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
    code->mov(dword[r15 + offsetof(A32JitState, FPSCR_nzcv)], nzcv);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPCompare32(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitFPCompare64(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitFPSingleToDouble(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        DenormalsAreZero32(code, result, gpr_scratch.cvt32());
    }
    code->cvtss2sd(result, result);
    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        FlushToZero64(code, result, gpr_scratch);
    }
    if (A32::LocationDescriptor{block.Location()}.FPSCR().DN()) {
        DefaultNaN64(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPDoubleToSingle(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm result = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg64 gpr_scratch = reg_alloc.ScratchGpr();

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        DenormalsAreZero64(code, result, gpr_scratch);
    }
    code->cvtsd2ss(result, result);
    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
        FlushToZero32(code, result, gpr_scratch.cvt32());
    }
    if (A32::LocationDescriptor{block.Location()}.FPSCR().DN()) {
        DefaultNaN32(code, result);
    }

    reg_alloc.DefineValue(inst, result);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPSingleToS32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = reg_alloc.ScratchXmm();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // Conversion to double is lossless, and allows for clamping.

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
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

template <typename PCT>
void EmitX64<PCT>::EmitFPSingleToU32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
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

    if (A32::LocationDescriptor{block.Location()}.FPSCR().RMode() != A32::FPSCR::RoundingMode::TowardsZero && !round_towards_zero) {
        if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
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

        if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
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

template <typename PCT>
void EmitX64<PCT>::EmitFPDoubleToS32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = reg_alloc.ScratchXmm();
    Xbyak::Reg32 gpr_scratch = reg_alloc.ScratchGpr().cvt32();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.

    if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
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

template <typename PCT>
void EmitX64<PCT>::EmitFPDoubleToU32(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Xmm from = reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Reg32 to = reg_alloc.ScratchGpr().cvt32();
    Xbyak::Xmm xmm_scratch = reg_alloc.ScratchXmm();
    Xbyak::Reg32 gpr_scratch = reg_alloc.ScratchGpr().cvt32();
    bool round_towards_zero = args[1].GetImmediateU1();

    // ARM saturates on conversion; this differs from x64 which returns a sentinel value.
    // TODO: Use VCVTPD2UDQ when AVX512VL is available.
    // FIXME: Inexact exception not correctly signalled with the below code

    if (A32::LocationDescriptor{block.Location()}.FPSCR().RMode() != A32::FPSCR::RoundingMode::TowardsZero && !round_towards_zero) {
        if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
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

        if (A32::LocationDescriptor{block.Location()}.FPSCR().FTZ()) {
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

template <typename PCT>
void EmitX64<PCT>::EmitFPS32ToSingle(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 from = reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Xmm to = reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code->cvtsi2ss(to, from);

    reg_alloc.DefineValue(inst, to);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPU32ToSingle(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitFPS32ToDouble(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
    auto args = reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 from = reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Xmm to = reg_alloc.ScratchXmm();
    bool round_to_nearest = args[1].GetImmediateU1();
    ASSERT_MSG(!round_to_nearest, "round_to_nearest unimplemented");

    code->cvtsi2sd(to, from);

    reg_alloc.DefineValue(inst, to);
}

template <typename PCT>
void EmitX64<PCT>::EmitFPU32ToDouble(RegAlloc& reg_alloc, IR::Block&, IR::Inst* inst) {
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

template <typename PCT>
void EmitX64<PCT>::EmitAddCycles(size_t cycles) {
    ASSERT(cycles < std::numeric_limits<u32>::max());
    code->sub(qword[r15 + offsetof(A32JitState, cycles_remaining)], static_cast<u32>(cycles));
}

template <typename PCT>
Xbyak::Label EmitX64<PCT>::EmitCond(IR::Cond cond) {
    Xbyak::Label label;

    const Xbyak::Reg32 cpsr = eax;
    code->mov(cpsr, dword[r15 + offsetof(A32JitState, CPSR_nzcv)]);

    constexpr size_t n_shift = 31;
    constexpr size_t z_shift = 30;
    constexpr size_t c_shift = 29;
    constexpr size_t v_shift = 28;
    constexpr u32 n_mask = 1u << n_shift;
    constexpr u32 z_mask = 1u << z_shift;
    constexpr u32 c_mask = 1u << c_shift;
    constexpr u32 v_mask = 1u << v_shift;

    switch (cond) {
    case IR::Cond::EQ: //z
        code->test(cpsr, z_mask);
        code->jnz(label);
        break;
    case IR::Cond::NE: //!z
        code->test(cpsr, z_mask);
        code->jz(label);
        break;
    case IR::Cond::CS: //c
        code->test(cpsr, c_mask);
        code->jnz(label);
        break;
    case IR::Cond::CC: //!c
        code->test(cpsr, c_mask);
        code->jz(label);
        break;
    case IR::Cond::MI: //n
        code->test(cpsr, n_mask);
        code->jnz(label);
        break;
    case IR::Cond::PL: //!n
        code->test(cpsr, n_mask);
        code->jz(label);
        break;
    case IR::Cond::VS: //v
        code->test(cpsr, v_mask);
        code->jnz(label);
        break;
    case IR::Cond::VC: //!v
        code->test(cpsr, v_mask);
        code->jz(label);
        break;
    case IR::Cond::HI: { //c & !z
        code->and_(cpsr, z_mask | c_mask);
        code->cmp(cpsr, c_mask);
        code->je(label);
        break;
    }
    case IR::Cond::LS: { //!c | z
        code->and_(cpsr, z_mask | c_mask);
        code->cmp(cpsr, c_mask);
        code->jne(label);
        break;
    }
    case IR::Cond::GE: { // n == v
        code->and_(cpsr, n_mask | v_mask);
        code->jz(label);
        code->cmp(cpsr, n_mask | v_mask);
        code->je(label);
        break;
    }
    case IR::Cond::LT: { // n != v
        Xbyak::Label fail;
        code->and_(cpsr, n_mask | v_mask);
        code->jz(fail);
        code->cmp(cpsr, n_mask | v_mask);
        code->jne(label);
        code->L(fail);
        break;
    }
    case IR::Cond::GT: { // !z & (n == v)
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
    case IR::Cond::LE: { // z | (n != v)
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

template <typename PCT>
void EmitX64<PCT>::EmitCondPrelude(const IR::Block& block) {
    if (block.GetCondition() == IR::Cond::AL) {
        ASSERT(!block.HasConditionFailedLocation());
        return;
    }

    ASSERT(block.HasConditionFailedLocation());

    Xbyak::Label pass = EmitCond(block.GetCondition());
    EmitAddCycles(block.ConditionFailedCycleCount());
    EmitTerminal(IR::Term::LinkBlock{block.ConditionFailedLocation()}, block.Location());
    code->L(pass);
}

template <typename PCT>
void EmitX64<PCT>::EmitTerminal(IR::Terminal terminal, IR::LocationDescriptor initial_location) {
    Common::VisitVariant<void>(terminal, [this, &initial_location](auto x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (!std::is_same_v<T, IR::Term::Invalid>) {
            this->EmitTerminalImpl(x, initial_location);
        } else {
            ASSERT_MSG(false, "Invalid terminal");
        }
    });
}

template <typename PCT>
void EmitX64<PCT>::Patch(const IR::LocationDescriptor& desc, CodePtr bb) {
    const CodePtr save_code_ptr = code->getCurr();
    const PatchInformation& patch_info = patch_information[desc];

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

template <typename PCT>
void EmitX64<PCT>::Unpatch(const IR::LocationDescriptor& desc) {
    Patch(desc, nullptr);
}

template <typename PCT>
void EmitX64<PCT>::ClearCache() {
    block_ranges.clear();
    block_descriptors.clear();
    patch_information.clear();
}

template <typename PCT>
void EmitX64<PCT>::InvalidateCacheRanges(const boost::icl::interval_set<PCT>& ranges) {
    // Remove cached block descriptors and patch information overlapping with the given range.
    std::unordered_set<IR::LocationDescriptor> erase_locations;
    for (auto invalidate_interval : ranges) {
        auto pair = block_ranges.equal_range(invalidate_interval);
        for (auto it = pair.first; it != pair.second; ++it) {
            for (const auto &descriptor : it->second) {
                erase_locations.insert(descriptor);
            }
        }
    }
    for (const auto &descriptor : erase_locations) {
        auto it = block_descriptors.find(descriptor);
        if (it == block_descriptors.end()) {
            continue;
        }

        if (patch_information.count(descriptor)) {
            Unpatch(descriptor);
        }
        block_ranges.subtract(std::make_pair(it->second.range, std::set<IR::LocationDescriptor>{descriptor}));
        block_descriptors.erase(it);
    }
}

template class EmitX64<u32>;
template class EmitX64<u64>;

} // namespace BackendX64
} // namespace Dynarmic
