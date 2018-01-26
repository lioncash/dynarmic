/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <ostream>
#include <fmt/format.h>

#include "frontend/ir/location_descriptor.h"

namespace Dynarmic::IR {

std::ostream& operator<<(std::ostream& o, const LocationDescriptor& descriptor) {
    o << fmt::format("{{{:016x}}}", descriptor.Value());
    return o;
}

} // namespace Dynarmic::IR
