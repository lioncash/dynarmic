/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::Common::mp {

namespace detail {

template<class... L>
struct append_impl;

template<template<class...> class LT, class... T1, class... T2>
struct append_impl<LT<T1...>, T2...> {
    using type = LT<T1..., T2...>;
};

} // namespace detail

/// Append items T to list L
template<class L, class... T>
using append = typename detail::append_impl<L, T...>::type;

} // namespace Dynarmic::Common::mp
