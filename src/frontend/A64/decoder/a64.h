/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <vector>

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
        // If a matcher has more bits in its mask it is more specific, so it should come first.
        return Common::BitCount(matcher1.GetMask()) > Common::BitCount(matcher2.GetMask());
    });

    // Exceptions to the above rule of thumb.
    const std::set<std::string> comes_first {
        "MOVI, MVNI, ORR, BIC (vector, immediate)",
        "FMOV (vector, immediate)",
        "Unallocated SIMD modified immediate",
    };

    std::stable_partition(table.begin(), table.end(), [&](const auto& matcher) {
        return comes_first.count(matcher.GetName()) > 0;
    });

    return table;
}

template<typename Visitor>
std::optional<std::reference_wrapper<const Matcher<Visitor>>> Decode(u32 instruction) {
    static const auto table = GetDecodeTable<Visitor>();

    const auto matches_instruction = [instruction](const auto& matcher) { return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? std::optional<std::reference_wrapper<const Matcher<Visitor>>>(*iter) : std::nullopt;
}

} // namespace Dynarmic::A64
