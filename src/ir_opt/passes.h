/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <dynarmic/A32/callbacks.h>

namespace Dynarmic {
namespace IR {
class Block;
}
}

namespace Dynarmic {
namespace Optimization {

void A32GetSetElimination(IR::Block& block);
void A32ConstantMemoryReads(IR::Block& block, const A32::UserCallbacks::Memory& memory_callbacks);
void ConstantPropagation(IR::Block& block);
void DeadCodeElimination(IR::Block& block);
void VerificationPass(const IR::Block& block);

} // namespace Optimization
} // namespace Dynarmic
