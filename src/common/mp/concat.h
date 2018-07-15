/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/mp/list.h"

namespace Dynarmic::Common::mp {

namespace detail {

template<class... L>
struct concat_impl;

template<>
struct concat_impl<> {
    using type = list<>;
};

template<class L>
struct concat_impl<L> {
    using type = L;
};

template<template<class...> class LT, class... T1, class... T2, class... Ls>
struct concat_impl<LT<T1...>, LT<T2...>, Ls...> {
    using type = typename concat_impl<LT<T1..., T2...>, Ls...>::type;
};

template<template<class...> class LT,
         class... T1, class... T2, class... T3, class... T4, class... T5, class... T6, class... T7, class... T8,
         class... T9, class... T10, class... T11, class... T12, class... T13, class... T14, class... T15, class... T16,
         class... Ls>
struct concat_impl<
        LT<T1...>, LT<T2...>, LT<T3...>, LT<T4...>, LT<T5...>, LT<T6...>, LT<T7...>, LT<T8...>,
        LT<T9...>, LT<T10...>, LT<T11...>, LT<T12...>, LT<T13...>, LT<T14...>, LT<T15...>, LT<T16...>,
        Ls...>
{
    using type = typename concat_impl<
        LT<
            T1..., T2..., T3..., T4..., T5..., T6..., T7..., T8...,
            T9..., T10..., T11..., T12..., T13..., T14..., T15..., T16...
        >,
        Ls...
    >::type;
};

} // namespace detail

/// Concatenate lists together
template<class... L>
using concat = typename detail::concat_impl<L...>::type;

} // namespace Dynarmic::Common::mp
