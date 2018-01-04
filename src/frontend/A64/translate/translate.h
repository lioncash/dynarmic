/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */
#pragma once

#include "common/common_types.h"

namespace Dynarmic {

namespace IR {
class Block;
} // namespace IR

namespace A64 {

class LocationDescriptor;

using MemoryReadCodeFuncType = u32 (*)(u64 vaddr);

/**
 * This function translates instructions in memory into our intermediate representation.
 * @param descriptor The starting location of the basic block. Includes information like PC, Thumb state, &c.
 * @param memory_read_code The function we should use to read emulated memory.
 * @return A translated basic block in the intermediate representation.
 */
IR::Block Translate(LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code);

} // namespace A64
} // namespace Dynarmic
