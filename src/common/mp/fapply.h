/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::Common::mp {

namespace detail {

template<template<class...> class F, class L>
struct fapply_impl;

template<template<class...> class F, template<class...> class LT, class... T>
struct fapply_impl<F, LT<T...>> {
    using type = F<T...>;
};

} // namespace detail

/// Invokes metafunction F where the arguments are all the members of list L
template<template<class...> class F, class L>
using fapply = typename detail::fapply_impl<F, L>::type;

} // namespace Dynarmic::Common::mp
