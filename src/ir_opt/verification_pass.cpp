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
#include "ir_opt/passes.h"

namespace Dynarmic {
namespace Optimization {

void VerificationPass(const IR::Block& block) {
    for (const auto& inst : block) {
        for (size_t i = 0; i < inst.NumArgs(); i++) {
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
        for (size_t i = 0; i < inst.NumArgs(); i++) {
            if (!inst.GetArg(i).IsImmediate()) {
                actual_uses[inst.GetArg(i).GetInst()]++;
            }
        }
    }

    for (const auto& pair : actual_uses) {
        ASSERT(pair.first->UseCount() == pair.second);
    }
}

} // namespace Optimization
} // namespace Dynarmic
