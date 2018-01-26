/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/iterator_util.h"
#include "frontend/ir/basic_block.h"
#include "ir_opt/passes.h"

namespace Dynarmic::Optimization {

void DeadCodeElimination(IR::Block& block) {
    // We iterate over the instructions in reverse order.
    // This is because removing an instruction reduces the number of uses for earlier instructions.
    for (auto& inst : Common::Reverse(block)) {
        if (!inst.HasUses() && !inst.MayHaveSideEffects()) {
            inst.Invalidate();
        }
    }
}

} // namespace Dynarmic
