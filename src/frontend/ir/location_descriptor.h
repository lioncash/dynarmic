/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <functional>
#include <iosfwd>

#include "common/common_types.h"

namespace Dynarmic::IR {

class LocationDescriptor {
public:
    explicit LocationDescriptor(u64 value) : value(value) {}

    bool operator == (const LocationDescriptor& o) const {
        return value == o.Value();
    }

    bool operator != (const LocationDescriptor& o) const {
        return !operator==(o);
    }

    u64 Value() const { return value; }

private:
    u64 value;
};

std::ostream& operator<<(std::ostream& o, const LocationDescriptor& descriptor);

} // namespace Dynarmic::IR

namespace std {
template <>
struct less<Dynarmic::IR::LocationDescriptor> {
    bool operator()(const Dynarmic::IR::LocationDescriptor& x, const Dynarmic::IR::LocationDescriptor& y) const noexcept {
        return x.Value() < y.Value();
    }
};
template <>
struct hash<Dynarmic::IR::LocationDescriptor> {
    size_t operator()(const Dynarmic::IR::LocationDescriptor& x) const noexcept {
        return std::hash<u64>()(x.Value());
    }
};
} // namespace std
