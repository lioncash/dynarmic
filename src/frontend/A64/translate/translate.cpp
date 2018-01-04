/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/translate/impl/impl.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"

namespace Dynarmic {
namespace A64 {

IR::Block Translate(LocationDescriptor descriptor, MemoryReadCodeFuncType /*memory_read_code*/) {
    // TODO
    return IR::Block{descriptor};
}

} // namespace A64
} // namespace Dynarmic
