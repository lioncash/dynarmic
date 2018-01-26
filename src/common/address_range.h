/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/common_types.h"

namespace Dynarmic::Common {

struct AddressRange {
    u32 start_address;
    size_t length;

    // Does this interval overlap with [from, to)?
    bool Overlaps(u32 from, u32 to) const {
        return start_address <= to && from <= start_address + length;
    }
};

} // namespace Dynarmic::Common
