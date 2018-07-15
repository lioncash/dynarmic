/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <type_traits>

#include "common/mp/list.h"

namespace Dynarmic::Common::mp {

namespace detail {

template<class VL>
struct vllift_impl{};

template<class T, T... values>
struct vllift_impl<std::integer_sequence<T, values...>> {
    using type = list<std::integral_constant<T, values>...>;
};

} // namespace detail

/// Lifts values in value list VL to create a type list.
template<class VL>
using vllift = typename detail::vllift_impl<VL>::type;

} // namespace Dynarmic::Common::mp
