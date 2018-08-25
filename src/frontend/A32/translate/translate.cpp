/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A32/location_descriptor.h"
#include "frontend/A32/translate/translate.h"
#include "frontend/ir/basic_block.h"

namespace Dynarmic::A32 {

IR::Block TranslateArm(LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code, const TranslationOptions& options);
IR::Block TranslateThumb(LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code, const TranslationOptions& options);

IR::Block Translate(LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code, const TranslationOptions& options) {
    return (descriptor.TFlag() ? TranslateThumb : TranslateArm)(descriptor, memory_read_code, options);
}

bool TranslateSingleArmInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction);
bool TranslateSingleThumbInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction);

bool TranslateSingleInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction) {
    return (descriptor.TFlag() ? TranslateSingleThumbInstruction : TranslateSingleArmInstruction)(block, descriptor, instruction);
}

} // namespace Dynarmic::A32
