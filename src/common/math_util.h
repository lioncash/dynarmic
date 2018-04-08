/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <utility>

namespace Dynarmic {
namespace Common {

/**
 * This function is a workaround for a bug in MSVC 19.12 where fold expressions
 * do not work when the /permissive- flag is enabled.
 */
template<typename T, typename... Ts>
constexpr T Sum(T first, Ts&&... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;
    } else {
        return first + Sum(std::forward<Ts>(rest)...);
    }
}

} // namespace Common
} // namespace Dynarmic
