/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend_x64/a64_jitstate.h"
#include "frontend/A64/location_descriptor.h"

namespace Dynarmic {
namespace BackendX64 {

u64 A64JitState::GetUniqueHash() const {
    u64 fpcr_u64 = static_cast<u64>(fpcr & A64::LocationDescriptor::FPCR_MASK) << 37;
    u64 pc_u64 = pc & A64::LocationDescriptor::PC_MASK;
    return pc_u64 | fpcr_u64;
}

} // namespace BackendX64
} // namespace Dynarmic
