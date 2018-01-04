/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <ostream>
#include <fmt/format.h>

#include "frontend/A64/location_descriptor.h"

namespace Dynarmic {
namespace A64 {

std::ostream& operator<<(std::ostream& o, const LocationDescriptor& loc) {
    o << fmt::format("{{{}, {}}}", loc.PC(), loc.FPCR().Value());
    return o;
}

} // namespace A64
} // namespace Dynarmic
