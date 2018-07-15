/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <type_traits>

#include "common/mp/cartesian_product.h"

using namespace Dynarmic::Common::mp;

static_assert(
    std::is_same_v<
        cartesian_product<list<int, bool>, list<double, float>, list<char, unsigned>>,
        list<
            list<int, double, char>,
            list<int, double, unsigned>,
            list<int, float, char>,
            list<int, float, unsigned>,
            list<bool, double, char>,
            list<bool, double, unsigned>,
            list<bool, float, char>,
            list<bool, float, unsigned>
        >
    >
);
