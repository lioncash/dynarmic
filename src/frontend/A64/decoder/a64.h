/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include <boost/optional.hpp>

#include "common/bit_util.h"
#include "common/common_types.h"
#include "frontend/decoder/decoder_detail.h"
#include "frontend/decoder/matcher.h"

namespace Dynarmic::A64 {

template <typename Visitor>
using Matcher = Decoder::Matcher<Visitor, u32>;

template <typename Visitor>
std::vector<Matcher<Visitor>> GetDecodeTable() {
    std::vector<Matcher<Visitor>> table = {
#define INST(fn, name, bitstring) Decoder::detail::detail<Matcher<Visitor>>::GetMatcher(&Visitor::fn, name, bitstring),
#include "a64.inc"
#undef INST
    };

    std::stable_sort(table.begin(), table.end(), [](const auto& matcher1, const auto& matcher2) {
        // If the matchers aren't related, keep their relative positions the same.
        if ((matcher1.GetMask() & ~matcher2.GetMask()) && (~matcher1.GetMask() & matcher2.GetMask()))
            return false;
        // If a matcher has more bits in its mask it is more specific, so it should come first.
        return Common::BitCount(matcher1.GetMask()) > Common::BitCount(matcher2.GetMask());
    });

    return table;
}

template<typename Visitor>
boost::optional<const Matcher<Visitor>&> Decode(u32 instruction) {
    static const auto table = GetDecodeTable<Visitor>();

    const auto matches_instruction = [instruction](const auto& matcher) { return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? boost::optional<const Matcher<Visitor>&>(*iter) : boost::none;
}

} // namespace Dynarmic::A64
