/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::Common {

template <typename T>
constexpr char SignToChar(T value) {
    return value >= 0 ? '+' : '-';
}

} // namespace Dynarmic::Common
