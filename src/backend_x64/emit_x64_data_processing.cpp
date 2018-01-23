/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic {
namespace BackendX64 {

using namespace Xbyak::util;

template <typename JST>
void EmitX64<JST>::EmitPack2x32To1x64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 lo = ctx.reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg64 hi = ctx.reg_alloc.UseScratchGpr(args[1]);

    code->shl(hi, 32);
    code->mov(lo.cvt32(), lo.cvt32()); // Zero extend to 64-bits
    code->or_(lo, hi);

    ctx.reg_alloc.DefineValue(inst, lo);
}

template <typename JST>
void EmitX64<JST>::EmitLeastSignificantWord(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.DefineValue(inst, args[0]);
}

template <typename JST>
void EmitX64<JST>::EmitMostSignificantWord(EmitContext& ctx, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->shr(result, 32);

    if (carry_inst) {
        Xbyak::Reg64 carry = ctx.reg_alloc.ScratchGpr();
        code->setc(carry.cvt8());
        ctx.reg_alloc.DefineValue(carry_inst, carry);
        ctx.EraseInstruction(carry_inst);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitLeastSignificantHalf(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.DefineValue(inst, args[0]);
}

template <typename JST>
void EmitX64<JST>::EmitLeastSignificantByte(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.DefineValue(inst, args[0]);
}

template <typename JST>
void EmitX64<JST>::EmitMostSignificantBit(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
    // TODO: Flag optimization
    code->shr(result, 31);
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitIsZero32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
    // TODO: Flag optimization
    code->test(result, result);
    code->sete(result.cvt8());
    code->movzx(result, result.cvt8());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitIsZero64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    // TODO: Flag optimization
    code->test(result, result);
    code->sete(result.cvt8());
    code->movzx(result, result.cvt8());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitTestBit(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    ASSERT(args[1].IsImmediate());
    // TODO: Flag optimization
    code->bt(result, args[1].GetImmediateU8());
    code->setc(result.cvt8());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
static void EmitConditionalSelect(BlockOfCode* code, EmitContext& ctx, IR::Inst* inst, int bitsize) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 nzcv = ctx.reg_alloc.ScratchGpr({HostLoc::RAX}).cvt32();
    Xbyak::Reg then_ = ctx.reg_alloc.UseGpr(args[1]).changeBit(bitsize);
    Xbyak::Reg else_ = ctx.reg_alloc.UseScratchGpr(args[2]).changeBit(bitsize);

    code->mov(nzcv, dword[r15 + code->GetJitStateInfo().offsetof_CPSR_nzcv]);
    // TODO: Flag optimization
    code->shr(nzcv, 28);
    code->imul(nzcv, nzcv, 0b00010000'10000001);
    code->and_(nzcv.cvt8(), 1);
    code->add(nzcv.cvt8(), 0x7F); // restore OF
    code->sahf(); // restore SF, ZF, CF

    switch (args[0].GetImmediateCond()) {
    case IR::Cond::EQ: //z
        code->cmovz(else_, then_);
        break;
    case IR::Cond::NE: //!z
        code->cmovnz(else_, then_);
        break;
    case IR::Cond::CS: //c
        code->cmovc(else_, then_);
        break;
    case IR::Cond::CC: //!c
        code->cmovnc(else_, then_);
        break;
    case IR::Cond::MI: //n
        code->cmovs(else_, then_);
        break;
    case IR::Cond::PL: //!n
        code->cmovns(else_, then_);
        break;
    case IR::Cond::VS: //v
        code->cmovo(else_, then_);
        break;
    case IR::Cond::VC: //!v
        code->cmovno(else_, then_);
        break;
    case IR::Cond::HI: //c & !z
        code->cmc();
        code->cmova(else_, then_);
        break;
    case IR::Cond::LS: //!c | z
        code->cmc();
        code->cmovna(else_, then_);
        break;
    case IR::Cond::GE: // n == v
        code->cmovge(else_, then_);
        break;
    case IR::Cond::LT: // n != v
        code->cmovl(else_, then_);
        break;
    case IR::Cond::GT: // !z & (n == v)
        code->cmovg(else_, then_);
        break;
    case IR::Cond::LE: // z | (n != v)
        code->cmovle(else_, then_);
        break;
    case IR::Cond::AL:
    case IR::Cond::NV:
        code->mov(else_, then_);
        break;
    default:
        ASSERT_MSG(false, "Invalid cond %zu", static_cast<size_t>(args[0].GetImmediateCond()));
    }

    ctx.reg_alloc.DefineValue(inst, else_);
}

template <typename JST>
void EmitX64<JST>::EmitConditionalSelect32(EmitContext& ctx, IR::Inst* inst) {
    EmitConditionalSelect<JST>(code, ctx, inst, 32);
}

template <typename JST>
void EmitX64<JST>::EmitConditionalSelect64(EmitContext& ctx, IR::Inst* inst) {
    EmitConditionalSelect<JST>(code, ctx, inst, 64);
}

template <typename JST>
void EmitX64<JST>::EmitLogicalShiftLeft32(EmitContext& ctx, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    // TODO: Consider using BMI2 instructions like SHLX when arm-in-host flags is implemented.

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            u8 shift = shift_arg.GetImmediateU8();

            if (shift <= 31) {
                code->shl(result, shift);
            } else {
                code->xor_(result, result);
            }

            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 zero = ctx.reg_alloc.ScratchGpr().cvt32();

            // The 32-bit x64 SHL instruction masks the shift count by 0x1F before performing the shift.
            // ARM differs from the behaviour: It does not mask the count, so shifts above 31 result in zeros.

            code->shl(result, code->cl);
            code->xor_(zero, zero);
            code->cmp(code->cl, 32);
            code->cmovnb(result, zero);

            ctx.reg_alloc.DefineValue(inst, result);
        }
    } else {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt32();

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

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt32();

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

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        }
    }
}

template <typename JST>
void EmitX64<JST>::EmitLogicalShiftLeft64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];

    if (shift_arg.IsImmediate()) {
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);
        u8 shift = shift_arg.GetImmediateU8();

        if (shift < 64) {
            code->shl(result, shift);
        } else {
            code->xor_(result.cvt32(), result.cvt32());
        }

        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);
        Xbyak::Reg64 zero = ctx.reg_alloc.ScratchGpr();

        // The x64 SHL instruction masks the shift count by 0x1F before performing the shift.
        // ARM differs from the behaviour: It does not mask the count, so shifts above 31 result in zeros.

        code->shl(result, code->cl);
        code->xor_(zero.cvt32(), zero.cvt32());
        code->cmp(code->cl, 64);
        code->cmovnb(result, zero);

        ctx.reg_alloc.DefineValue(inst, result);
    }
}

template <typename JST>
void EmitX64<JST>::EmitLogicalShiftRight32(EmitContext& ctx, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            u8 shift = shift_arg.GetImmediateU8();

            if (shift <= 31) {
                code->shr(result, shift);
            } else {
                code->xor_(result, result);
            }

            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 zero = ctx.reg_alloc.ScratchGpr().cvt32();

            // The 32-bit x64 SHR instruction masks the shift count by 0x1F before performing the shift.
            // ARM differs from the behaviour: It does not mask the count, so shifts above 31 result in zeros.

            code->shr(result, code->cl);
            code->xor_(zero, zero);
            code->cmp(code->cl, 32);
            code->cmovnb(result, zero);

            ctx.reg_alloc.DefineValue(inst, result);
        }
    } else {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt32();

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

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt32();

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

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        }
    }
}

template <typename JST>
void EmitX64<JST>::EmitLogicalShiftRight64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];

    if (shift_arg.IsImmediate()) {
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);
        u8 shift = shift_arg.GetImmediateU8();

        if (shift < 64) {
            code->shr(result, shift);
        } else {
            code->xor_(result.cvt32(), result.cvt32());
        }

        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);
        Xbyak::Reg64 zero = ctx.reg_alloc.ScratchGpr();

        // The x64 SHR instruction masks the shift count by 0x1F before performing the shift.
        // ARM differs from the behaviour: It does not mask the count, so shifts above 31 result in zeros.

        code->shr(result, code->cl);
        code->xor_(zero.cvt32(), zero.cvt32());
        code->cmp(code->cl, 64);
        code->cmovnb(result, zero);

        ctx.reg_alloc.DefineValue(inst, result);
    }
}

template <typename JST>
void EmitX64<JST>::EmitArithmeticShiftRight32(EmitContext& ctx, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();

            code->sar(result, u8(shift < 31 ? shift : 31));

            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.UseScratch(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg32 const31 = ctx.reg_alloc.ScratchGpr().cvt32();

            // The 32-bit x64 SAR instruction masks the shift count by 0x1F before performing the shift.
            // ARM differs from the behaviour: It does not mask the count.

            // We note that all shift values above 31 have the same behaviour as 31 does, so we saturate `shift` to 31.
            code->mov(const31, 31);
            code->movzx(code->ecx, code->cl);
            code->cmp(code->ecx, u32(31));
            code->cmovg(code->ecx, const31);
            code->sar(result, code->cl);

            ctx.reg_alloc.DefineValue(inst, result);
        }
    } else {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt8();

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

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt8();

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

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        }
    }
}

template <typename JST>
void EmitX64<JST>::EmitArithmeticShiftRight64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];

    if (shift_arg.IsImmediate()) {
        u8 shift = shift_arg.GetImmediateU8();
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);

        code->sar(result, u8(shift < 63 ? shift : 63));

        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ctx.reg_alloc.UseScratch(shift_arg, HostLoc::RCX);
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);
        Xbyak::Reg64 const63 = ctx.reg_alloc.ScratchGpr();

        // The 64-bit x64 SAR instruction masks the shift count by 0x3F before performing the shift.
        // ARM differs from the behaviour: It does not mask the count.

        // We note that all shift values above 63 have the same behaviour as 63 does, so we saturate `shift` to 63.
        code->mov(const63, 63);
        code->movzx(code->ecx, code->cl);
        code->cmp(code->ecx, u32(63));
        code->cmovg(code->ecx, const63);
        code->sar(result, code->cl);

        ctx.reg_alloc.DefineValue(inst, result);
    }
}

template <typename JST>
void EmitX64<JST>::EmitRotateRight32(EmitContext& ctx, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];
    auto& carry_arg = args[2];

    if (!carry_inst) {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();

            code->ror(result, u8(shift & 0x1F));

            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();

            // x64 ROR instruction does (shift & 0x1F) for us.
            code->ror(result, code->cl);

            ctx.reg_alloc.DefineValue(inst, result);
        }
    } else {
        if (shift_arg.IsImmediate()) {
            u8 shift = shift_arg.GetImmediateU8();
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt8();

            if (shift == 0) {
                // There is nothing more to do.
            } else if ((shift & 0x1F) == 0) {
                code->bt(result, u8(31));
                code->setc(carry);
            } else {
                code->ror(result, shift);
                code->setc(carry);
            }

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        } else {
            ctx.reg_alloc.UseScratch(shift_arg, HostLoc::RCX);
            Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(operand_arg).cvt32();
            Xbyak::Reg8 carry = ctx.reg_alloc.UseScratchGpr(carry_arg).cvt8();

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

            ctx.reg_alloc.DefineValue(carry_inst, carry);
            ctx.EraseInstruction(carry_inst);
            ctx.reg_alloc.DefineValue(inst, result);
        }
    }
}

template <typename JST>
void EmitX64<JST>::EmitRotateRight64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& operand_arg = args[0];
    auto& shift_arg = args[1];

    if (shift_arg.IsImmediate()) {
        u8 shift = shift_arg.GetImmediateU8();
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);

        code->ror(result, u8(shift & 0x3F));

        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ctx.reg_alloc.Use(shift_arg, HostLoc::RCX);
        Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(operand_arg);

        // x64 ROR instruction does (shift & 0x3F) for us.
        code->ror(result, code->cl);

        ctx.reg_alloc.DefineValue(inst, result);
    }
}

template <typename JST>
void EmitX64<JST>::EmitRotateRightExtended(EmitContext& ctx, IR::Inst* inst) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
    Xbyak::Reg8 carry = ctx.reg_alloc.UseScratchGpr(args[1]).cvt8();

    code->bt(carry.cvt32(), 0);
    code->rcr(result, 1);

    if (carry_inst) {
        code->setc(carry);

        ctx.reg_alloc.DefineValue(carry_inst, carry);
        ctx.EraseInstruction(carry_inst);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

const Xbyak::Reg64 INVALID_REG = Xbyak::Reg64(-1);

static Xbyak::Reg8 DoCarry(RegAlloc& reg_alloc, Argument& carry_in, IR::Inst* carry_out) {
    if (carry_in.IsImmediate()) {
        return carry_out ? reg_alloc.ScratchGpr().cvt8() : INVALID_REG.cvt8();
    } else {
        return carry_out ? reg_alloc.UseScratchGpr(carry_in).cvt8() : reg_alloc.UseGpr(carry_in).cvt8();
    }
}

static Xbyak::Reg64 DoNZCV(BlockOfCode* code, RegAlloc& reg_alloc, IR::Inst* nzcv_out) {
    if (!nzcv_out)
        return INVALID_REG;

    Xbyak::Reg64 nzcv = reg_alloc.ScratchGpr({HostLoc::RAX});
    code->xor_(nzcv.cvt32(), nzcv.cvt32());
    return nzcv;
}

static void EmitAdd(BlockOfCode* code, EmitContext& ctx, IR::Inst* inst, int bitsize) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);
    auto nzcv_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetNZCVFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& carry_in = args[2];

    Xbyak::Reg64 nzcv = DoNZCV(code, ctx.reg_alloc, nzcv_inst);
    Xbyak::Reg result = ctx.reg_alloc.UseScratchGpr(args[0]).changeBit(bitsize);
    Xbyak::Reg8 carry = DoCarry(ctx.reg_alloc, carry_in, carry_inst);
    Xbyak::Reg8 overflow = overflow_inst ? ctx.reg_alloc.ScratchGpr().cvt8() : INVALID_REG.cvt8();

    // TODO: Consider using LEA.

    if (args[1].IsImmediate() && args[1].GetType() == IR::Type::U32) {
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
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(bitsize);
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

    if (nzcv_inst) {
        code->lahf();
        code->seto(code->al);
        ctx.reg_alloc.DefineValue(nzcv_inst, nzcv);
        ctx.EraseInstruction(nzcv_inst);
    }
    if (carry_inst) {
        code->setc(carry);
        ctx.reg_alloc.DefineValue(carry_inst, carry);
        ctx.EraseInstruction(carry_inst);
    }
    if (overflow_inst) {
        code->seto(overflow);
        ctx.reg_alloc.DefineValue(overflow_inst, overflow);
        ctx.EraseInstruction(overflow_inst);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitAdd(code, ctx, inst, 32);
}

template <typename JST>
void EmitX64<JST>::EmitAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitAdd(code, ctx, inst, 64);
}

static void EmitSub(BlockOfCode* code, EmitContext& ctx, IR::Inst* inst, int bitsize) {
    auto carry_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetCarryFromOp);
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);
    auto nzcv_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetNZCVFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto& carry_in = args[2];

    Xbyak::Reg64 nzcv = DoNZCV(code, ctx.reg_alloc, nzcv_inst);
    Xbyak::Reg result = ctx.reg_alloc.UseScratchGpr(args[0]).changeBit(bitsize);
    Xbyak::Reg8 carry = DoCarry(ctx.reg_alloc, carry_in, carry_inst);
    Xbyak::Reg8 overflow = overflow_inst ? ctx.reg_alloc.ScratchGpr().cvt8() : INVALID_REG.cvt8();

    // TODO: Consider using LEA.
    // TODO: Optimize CMP case.
    // Note that x64 CF is inverse of what the ARM carry flag is here.

    if (args[1].IsImmediate() && args[1].GetType() == IR::Type::U32) {
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
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(bitsize);
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

    if (nzcv_inst) {
        code->cmc();
        code->lahf();
        code->seto(code->al);
        ctx.reg_alloc.DefineValue(nzcv_inst, nzcv);
        ctx.EraseInstruction(nzcv_inst);
    }
    if (carry_inst) {
        code->setnc(carry);
        ctx.reg_alloc.DefineValue(carry_inst, carry);
        ctx.EraseInstruction(carry_inst);
    }
    if (overflow_inst) {
        code->seto(overflow);
        ctx.reg_alloc.DefineValue(overflow_inst, overflow);
        ctx.EraseInstruction(overflow_inst);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitSub(code, ctx, inst, 32);
}

template <typename JST>
void EmitX64<JST>::EmitSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitSub(code, ctx, inst, 64);
}

template <typename JST>
void EmitX64<JST>::EmitMul32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
    if (args[1].IsImmediate()) {
        code->imul(result, result, args[1].GetImmediateU32());
    } else {
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->imul(result, *op_arg);
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitMul64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);

    code->imul(result, *op_arg);

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitAnd32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();

        code->and_(result, op_arg);
    } else {
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->and_(result, *op_arg);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitAnd64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);

    if (args[1].FitsInImmediateS32()) {
        u32 op_arg = u32(args[1].GetImmediateS32());

        code->and_(result, op_arg);
    } else {
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(64);

        code->and_(result, *op_arg);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitEor32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();

        code->xor_(result, op_arg);
    } else {
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->xor_(result, *op_arg);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitEor64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);

    if (args[1].FitsInImmediateS32()) {
        u32 op_arg = u32(args[1].GetImmediateS32());

        code->xor_(result, op_arg);
    } else {
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(64);

        code->xor_(result, *op_arg);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitOr32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();

    if (args[1].IsImmediate()) {
        u32 op_arg = args[1].GetImmediateU32();

        code->or_(result, op_arg);
    } else {
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(32);

        code->or_(result, *op_arg);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitOr64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);

    if (args[1].FitsInImmediateS32()) {
        u32 op_arg = u32(args[1].GetImmediateS32());

        code->or_(result, op_arg);
    } else {
        OpArg op_arg = ctx.reg_alloc.UseOpArg(args[1]);
        op_arg.setBit(64);

        code->or_(result, *op_arg);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitNot32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg32 result;
    if (args[0].IsImmediate()) {
        result = ctx.reg_alloc.ScratchGpr().cvt32();
        code->mov(result, u32(~args[0].GetImmediateU32()));
    } else {
        result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
        code->not_(result);
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitNot64(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg64 result;
    if (args[0].IsImmediate()) {
        result = ctx.reg_alloc.ScratchGpr();
        code->mov(result, ~args[0].GetImmediateU64());
    } else {
        result = ctx.reg_alloc.UseScratchGpr(args[0]);
        code->not_(result);
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitSignExtendByteToWord(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt32(), result.cvt8());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitSignExtendHalfToWord(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt32(), result.cvt16());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitSignExtendByteToLong(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt64(), result.cvt8());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitSignExtendHalfToLong(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movsx(result.cvt64(), result.cvt16());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitSignExtendWordToLong(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movsxd(result.cvt64(), result.cvt32());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitZeroExtendByteToWord(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt8());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitZeroExtendHalfToWord(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt16());
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitZeroExtendByteToLong(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt8()); // x64 zeros upper 32 bits on a 32-bit move
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitZeroExtendHalfToLong(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->movzx(result.cvt32(), result.cvt16()); // x64 zeros upper 32 bits on a 32-bit move
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitZeroExtendWordToLong(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->mov(result.cvt32(), result.cvt32()); // x64 zeros upper 32 bits on a 32-bit move
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitByteReverseWord(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
    code->bswap(result);
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitByteReverseHalf(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg16 result = ctx.reg_alloc.UseScratchGpr(args[0]).cvt16();
    code->rol(result, 8);
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitByteReverseDual(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    Xbyak::Reg64 result = ctx.reg_alloc.UseScratchGpr(args[0]);
    code->bswap(result);
    ctx.reg_alloc.DefineValue(inst, result);
}

template <typename JST>
void EmitX64<JST>::EmitCountLeadingZeros32(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (code->DoesCpuSupport(Xbyak::util::Cpu::tLZCNT)) {
        Xbyak::Reg32 source = ctx.reg_alloc.UseGpr(args[0]).cvt32();
        Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();

        code->lzcnt(result, source);

        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        Xbyak::Reg32 source = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
        Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();

        // The result of a bsr of zero is undefined, but zf is set after it.
        code->bsr(result, source);
        code->mov(source, 0xFFFFFFFF);
        code->cmovz(result, source);
        code->neg(result);
        code->add(result, 31);

        ctx.reg_alloc.DefineValue(inst, result);
    }
}

template <typename JST>
void EmitX64<JST>::EmitCountLeadingZeros64(EmitContext& ctx, IR::Inst* inst) {
   auto args = ctx.reg_alloc.GetArgumentInfo(inst);
   if (code->DoesCpuSupport(Xbyak::util::Cpu::tLZCNT)) {
       Xbyak::Reg64 source = ctx.reg_alloc.UseGpr(args[0]).cvt64();
       Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr().cvt64();

       code->lzcnt(result, source);

       ctx.reg_alloc.DefineValue(inst, result);
   } else {
       Xbyak::Reg64 source = ctx.reg_alloc.UseScratchGpr(args[0]).cvt64();
       Xbyak::Reg64 result = ctx.reg_alloc.ScratchGpr().cvt64();

       // The result of a bsr of zero is undefined, but zf is set after it.
       code->bsr(result, source);
       code->mov(source.cvt32(), 0xFFFFFFFF);
       code->cmovz(result.cvt32(), source.cvt32());
       code->neg(result.cvt32());
       code->add(result.cvt32(), 63);

       ctx.reg_alloc.DefineValue(inst, result);
   }
}

} // namespace BackendX64
} // namespace Dynarmic

#include "backend_x64/a32_jitstate.h"
#include "backend_x64/a64_jitstate.h"

template class Dynarmic::BackendX64::EmitX64<Dynarmic::BackendX64::A32JitState>;
template class Dynarmic::BackendX64::EmitX64<Dynarmic::BackendX64::A64JitState>;
