/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public icense version 2 or any later version.
 */

#include "backend/x64/block_of_code.h"
#include "backend/x64/emit_x64.h"
#include "common/common_types.h"
#include "common/crypto/sm4.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic::BackendX64 {

void EmitX64::EmitSM4AccessSubstitutionBox(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.HostCall(inst, args[0]);
    code.CallFunction(&Common::Crypto::SM4::AccessSubstitutionBox);
}

} // namespace Dynarmic::BackendX64
