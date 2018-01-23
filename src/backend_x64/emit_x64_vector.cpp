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

static void EmitVectorOperation(BlockOfCode* code, EmitContext& ctx, IR::Inst* inst, void (Xbyak::CodeGenerator::*fn)(const Xbyak::Mmx& mmx, const Xbyak::Operand&)) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    Xbyak::Xmm xmm_a = ctx.reg_alloc.UseScratchXmm(args[0]);
    Xbyak::Xmm xmm_b = ctx.reg_alloc.UseXmm(args[1]);

    (code->*fn)(xmm_a, xmm_b);

    ctx.reg_alloc.DefineValue(inst, xmm_a);
}

template <typename JST>
void EmitX64<JST>::EmitVectorAdd8(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddb);
}

template <typename JST>
void EmitX64<JST>::EmitVectorAdd16(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddw);
}

template <typename JST>
void EmitX64<JST>::EmitVectorAdd32(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddd);
}

template <typename JST>
void EmitX64<JST>::EmitVectorAdd64(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::paddq);
}

template <typename JST>
void EmitX64<JST>::EmitVectorAnd(EmitContext& ctx, IR::Inst* inst) {
    EmitVectorOperation(code, ctx, inst, &Xbyak::CodeGenerator::pand);
}

} // namespace BackendX64
} // namespace Dynarmic

#include "backend_x64/a32_jitstate.h"
#include "backend_x64/a64_jitstate.h"

template class Dynarmic::BackendX64::EmitX64<Dynarmic::BackendX64::A32JitState>;
template class Dynarmic::BackendX64::EmitX64<Dynarmic::BackendX64::A64JitState>;
