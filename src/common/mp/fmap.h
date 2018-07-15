/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::Common::mp {

namespace detail {

template<template<class...> class F, class L>
struct fmap_impl;

template<template<class...> class F, template<class...> class LT, class... T>
struct fmap_impl<F, LT<T...>> {
    using type = LT<F<T>...>;
};

} // namespace detail

/// Metafunction that applies each element of list L to metafunction F
template<template<class...> class F, class L>
using fmap = typename detail::fmap_impl<F, L>::type;

} // namespace Dynarmic::Common::mp
