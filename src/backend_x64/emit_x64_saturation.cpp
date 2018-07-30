/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <limits>

#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/mp/integer.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;
namespace mp = Dynarmic::Common::mp;

namespace {

enum class Op {
    Add,
    Sub,
};

template<Op op, size_t size>
void EmitSignedSaturatedOp(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst) {
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Reg result = ctx.reg_alloc.UseScratchGpr(args[0]);
    Xbyak::Reg addend = ctx.reg_alloc.UseGpr(args[1]);
    Xbyak::Reg overflow = ctx.reg_alloc.ScratchGpr();

    result.setBit(size);
    addend.setBit(size);
    overflow.setBit(size);

    constexpr u64 int_max = static_cast<u64>(std::numeric_limits<mp::signed_integer_of_size<size>>::max());
    if constexpr (size < 64) {
        code.xor_(overflow.cvt32(), overflow.cvt32());
        code.bt(result.cvt32(), size - 1);
        code.adc(overflow.cvt32(), int_max);
    } else {
        code.mov(overflow, int_max);
        code.bt(result, 63);
        code.adc(overflow, 0);
    }

    // overflow now contains 0x7F... if a was positive, or 0x80... if a was negative

    if constexpr (op == Op::Add) {
        code.add(result, addend);
    } else {
        code.sub(result, addend);
    }

    if constexpr (size < 64) {
        code.cmovo(result.cvt32(), overflow.cvt32());
    } else {
        code.cmovo(result, overflow);
    }

    if (overflow_inst) {
        code.seto(overflow.cvt8());

        ctx.reg_alloc.DefineValue(overflow_inst, overflow);
        ctx.EraseInstruction(overflow_inst);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

} // anonymous namespace

void EmitX64::EmitSignedSaturatedAdd8(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Add, 8>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedAdd16(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Add, 16>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Add, 32>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Add, 64>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSub8(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Sub, 8>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSub16(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Sub, 16>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSub32(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Sub, 32>(code, ctx, inst);
}

void EmitX64::EmitSignedSaturatedSub64(EmitContext& ctx, IR::Inst* inst) {
    EmitSignedSaturatedOp<Op::Sub, 64>(code, ctx, inst);
}

void EmitX64::EmitUnsignedSaturation(EmitContext& ctx, IR::Inst* inst) {
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    size_t N = args[1].GetImmediateU8();
    ASSERT(N <= 31);

    u32 saturated_value = (1u << N) - 1;

    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 reg_a = ctx.reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Reg32 overflow = ctx.reg_alloc.ScratchGpr().cvt32();

    // Pseudocode: result = clamp(reg_a, 0, saturated_value);
    code.xor_(overflow, overflow);
    code.cmp(reg_a, saturated_value);
    code.mov(result, saturated_value);
    code.cmovle(result, overflow);
    code.cmovbe(result, reg_a);

    if (overflow_inst) {
        code.seta(overflow.cvt8());

        ctx.reg_alloc.DefineValue(overflow_inst, overflow);
        ctx.EraseInstruction(overflow_inst);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

void EmitX64::EmitSignedSaturation(EmitContext& ctx, IR::Inst* inst) {
    auto overflow_inst = inst->GetAssociatedPseudoOperation(IR::Opcode::GetOverflowFromOp);

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    size_t N = args[1].GetImmediateU8();
    ASSERT(N >= 1 && N <= 32);

    if (N == 32) {
        if (overflow_inst) {
            auto no_overflow = IR::Value(false);
            overflow_inst->ReplaceUsesWith(no_overflow);
        }
        ctx.reg_alloc.DefineValue(inst, args[0]);
        return;
    }

    u32 mask = (1u << N) - 1;
    u32 positive_saturated_value = (1u << (N - 1)) - 1;
    u32 negative_saturated_value = 1u << (N - 1);
    u32 sext_negative_satured_value = Common::SignExtend(N, negative_saturated_value);

    Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 reg_a = ctx.reg_alloc.UseGpr(args[0]).cvt32();
    Xbyak::Reg32 overflow = ctx.reg_alloc.ScratchGpr().cvt32();
    Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr().cvt32();

    // overflow now contains a value between 0 and mask if it was originally between {negative,positive}_saturated_value.
    code.lea(overflow, code.ptr[reg_a.cvt64() + negative_saturated_value]);

    // Put the appropriate saturated value in result
    code.cmp(reg_a, positive_saturated_value);
    code.mov(tmp, positive_saturated_value);
    code.mov(result, sext_negative_satured_value);
    code.cmovg(result, tmp);

    // Do the saturation
    code.cmp(overflow, mask);
    code.cmovbe(result, reg_a);

    if (overflow_inst) {
        code.seta(overflow.cvt8());

        ctx.reg_alloc.DefineValue(overflow_inst, overflow);
        ctx.EraseInstruction(overflow_inst);
    }

    ctx.reg_alloc.DefineValue(inst, result);
}

} // namespace Dynarmic::BackendX64
