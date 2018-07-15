/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <map>
#include <type_traits>

#include "common/mp/list.h"

namespace Dynarmic::Common::mp {

template <typename KeyT, typename ValueT, typename Function, typename ...Values>
inline auto GenerateLookupTableFromList(Function f, list<Values...>) {
    static const std::array<std::pair<KeyT, ValueT>, sizeof...(Values)> pair_array{f(Values{})...};
    return std::map<KeyT, ValueT>(pair_array.begin(), pair_array.end());
}

} // namespace Dynarmic::Common::mp
