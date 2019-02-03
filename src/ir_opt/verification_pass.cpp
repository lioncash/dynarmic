/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <map>

#include "common/assert.h"
#include "common/common_types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"
#include "frontend/ir/type.h"
#include "ir_opt/passes.h"

namespace Dynarmic::Optimization {

void VerificationPass(const IR::Block& block) {
    for (const auto& inst : block) {
        const size_t num_args = inst.NumArgs();
        for (size_t i = 0; i < num_args; i++) {
            IR::Type t1 = inst.GetArg(i).GetType();
            IR::Type t2 = IR::GetArgTypeOf(inst.GetOpcode(), i);
            if (!IR::AreTypesCompatible(t1, t2)) {
                puts(IR::DumpBlock(block).c_str());
                ASSERT(false);
            }
        }
    }

    std::map<IR::Inst*, size_t> actual_uses;
    for (const auto& inst : block) {
        const size_t num_args = inst.NumArgs();
        for (size_t i = 0; i < num_args; i++) {
            if (!inst.GetArg(i).IsImmediate()) {
                actual_uses[inst.GetArg(i).GetInst()]++;
            }
        }
    }

    for (const auto& pair : actual_uses) {
        ASSERT(pair.first->UseCount() == pair.second);
    }
}

} // namespace Dynarmic::Optimization
