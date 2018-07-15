/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <type_traits>

namespace Dynarmic::Common::mp {

/// Lifts a value into a type
template<auto V>
using vlift = std::integral_constant<decltype(V), V>;

} // namespace Dynarmic::Common::mp
