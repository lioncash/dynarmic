/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <iosfwd>

#include "common/common_types.h"

namespace Dynarmic {
namespace IR {

class LocationDescriptor {
public:
    bool operator == (const LocationDescriptor& o) const {
        return value == o.value;
    }

    bool operator != (const LocationDescriptor& o) const {
        return !operator==(o);
    }

    u64 value;
};

std::ostream& operator<<(std::ostream& o, const LocationDescriptor& descriptor);

} // namespace A32
} // namespace Dynarmic

namespace std {
template <>
struct less<Dynarmic::IR::LocationDescriptor> {
    bool operator()(const Dynarmic::IR::LocationDescriptor& x, const Dynarmic::IR::LocationDescriptor& y) const {
        return x.value < y.value;
    }
};
template <>
struct hash<Dynarmic::IR::LocationDescriptor> {
    size_t operator()(const Dynarmic::IR::LocationDescriptor& x) const {
        return std::hash<u64>()(x.value);
    }
};
} // namespace std
