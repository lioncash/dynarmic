/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include <ostream>
#include <string>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "frontend/ir/type.h"

namespace Dynarmic::IR {

std::string GetNameOf(Type type) {
    static const std::array<const char*, 16> names = {
        "Void", "A32Reg", "A32ExtReg", "A64Reg", "A64Vec", "Opaque", "U1", "U8", "U16", "U32", "U64", "F32", "F64", "CoprocInfo", "NZCVFlags", "Cond"
    };
    const size_t index = static_cast<size_t>(type);
    if (index > names.size())
        return fmt::format("Unknown Type {}", index);
    return names.at(index);
}

bool AreTypesCompatible(Type t1, Type t2) {
    return t1 == t2 || t1 == Type::Opaque || t2 == Type::Opaque;
}

std::ostream& operator<<(std::ostream& o, Type type) {
    return o << GetNameOf(type);
}

} // namespace Dynarmic::IR
