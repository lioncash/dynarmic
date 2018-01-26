/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <string>

#include "common/common_types.h"

namespace Dynarmic::A32 {

std::string DisassembleArm(u32 instruction);
std::string DisassembleThumb16(u16 instruction);

} // namespace Dynarmic::A32
