/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <tuple>

namespace Dynarmic::Common::mp {

namespace detail {

template<class L>
struct to_tuple_impl;

template<template<class...> class LT, class... T>
struct to_tuple_impl<LT<T...>> {
    static constexpr auto value = std::make_tuple(static_cast<typename T::value_type>(T::value)...);
};

} // namespace detail

/// Metafunction that converts a list of metavalues to a tuple value.
template<class L>
constexpr auto to_tuple = detail::to_tuple_impl<L>::value;

} // namespace Dynarmic::Common::mp
