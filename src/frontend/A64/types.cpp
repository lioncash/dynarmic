/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include <ostream>

#include <fmt/format.h>

#include "common/bit_util.h"
#include "frontend/A64/types.h"

namespace Dynarmic {
namespace A64 {

const char* CondToString(Cond cond) {
    constexpr std::array<const char*, 16> cond_strs = {
        "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
    };
    return cond_strs.at(static_cast<size_t>(cond));
}

std::string RegToString(Reg reg) {
    if (reg == Reg::R31)
        return "sp|zr";
    return fmt::format("r{}", static_cast<size_t>(reg));
}

std::string VecToString(Vec vec) {
    return fmt::format("v{}", static_cast<size_t>(vec));
}

std::ostream& operator<<(std::ostream& o, Reg reg) {
    o << RegToString(reg);
    return o;
}

std::ostream& operator<<(std::ostream& o, Vec vec) {
    o << VecToString(vec);
    return o;
}

} // namespace A64
} // namespace Dynarmic
