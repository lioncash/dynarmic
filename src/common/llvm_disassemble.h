/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <string>

#include "common/common_types.h"

namespace Dynarmic::Common {

std::string DisassembleX64(const void* pos, const void* end);
std::string DisassembleAArch64(u32 instruction, u64 pc = 0);

} // namespace Dynarmic::Common
