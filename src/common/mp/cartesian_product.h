/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/mp/append.h"
#include "common/mp/bind.h"
#include "common/mp/concat.h"
#include "common/mp/fmap.h"
#include "common/mp/list.h"

namespace Dynarmic::Common::mp {

namespace detail {

template<class... Ls>
struct cartesian_product_impl{};

template<class RL>
struct cartesian_product_impl<RL> {
    using type = RL;
};

template<template<class...> class LT, class... RT, class... T1>
struct cartesian_product_impl<LT<RT...>, LT<T1...>> {
    using type = concat<
        fmap<bind<append, RT>::template type, list<T1...>>...
    >;
};

template<class RL, class L1, class L2, class... Ls>
struct cartesian_product_impl<RL, L1, L2, Ls...> {
    using type = typename cartesian_product_impl<
        typename cartesian_product_impl<RL, L1>::type,
        L2,
        Ls...
    >::type;
};

} // namespace detail

/// Produces the cartesian product of a set of lists
/// For example: 
/// cartesian_product<list<A, B>, list<D, E>> == list<list<A, D>, list<A, E>, list<B, D>, list<B, E>
template<typename L1, typename... Ls>
using cartesian_product = typename detail::cartesian_product_impl<fmap<list, L1>, Ls...>::type;

} // namespace Dynarmic::Common::mp
