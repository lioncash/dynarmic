/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <cstdint>
#include <string>

namespace Dynarmic {
namespace A32 {

std::string DisassembleArm(std::uint32_t instruction);
std::string DisassembleThumb16(std::uint16_t instruction);

} // namespace A32
} // namespace Dynarmic
