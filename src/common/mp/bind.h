/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::Common::mp {

/// Binds the first sizeof...(A) arguments of metafunction F with arguments A
template<template<class...> class F, class... A>
struct bind {
    template<class... T>
    using type = F<A..., T...>;
};

} // namespace Dynarmic::Common::mp
