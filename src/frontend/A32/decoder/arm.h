/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 *
 * Original version of table by Lioncash.
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

namespace Dynarmic::A32 {

template <typename Visitor>
using ArmMatcher = Decoder::Matcher<Visitor, u32>;

template <typename V>
std::vector<ArmMatcher<V>> GetArmDecodeTable() {
    std::vector<ArmMatcher<V>> table = {

#define INST(fn, name, bitstring) Decoder::detail::detail<ArmMatcher<V>>::GetMatcher(&V::fn, name, bitstring),
#include "arm.inc"
#undef INST

    };

    // If a matcher has more bits in its mask it is more specific, so it should come first.
    std::stable_sort(table.begin(), table.end(), [](const auto& matcher1, const auto& matcher2) {
        return Common::BitCount(matcher1.GetMask()) > Common::BitCount(matcher2.GetMask());
    });

    return table;
}

template<typename V>
boost::optional<const ArmMatcher<V>&> DecodeArm(u32 instruction) {
    static const auto table = GetArmDecodeTable<V>();

    const auto matches_instruction = [instruction](const auto& matcher) { return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? boost::optional<const ArmMatcher<V>&>(*iter) : boost::none;
}

} // namespace Dynarmic::A32
