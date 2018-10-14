/* This file is part of the dynarmic project.
 * Copyright (c) 2032 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>


#include "common/common_types.h"
#include "frontend/decoder/decoder_detail.h"
#include "frontend/decoder/matcher.h"

namespace Dynarmic::A32 {

template <typename Visitor>
using VFP2Matcher = Decoder::Matcher<Visitor, u32>;

template<typename V>
std::optional<std::reference_wrapper<const VFP2Matcher<V>>> DecodeVFP2(u32 instruction) {
    static const std::vector<VFP2Matcher<V>> table = {

#define INST(fn, name, bitstring) Decoder::detail::detail<VFP2Matcher<V>>::GetMatcher(&V::fn, name, bitstring),
#include "vfp2.inc"
#undef INST

    };

    if ((instruction & 0xF0000000) == 0xF0000000)
        return std::nullopt; // Don't try matching any unconditional instructions.

    const auto matches_instruction = [instruction](const auto& matcher){ return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? std::optional<std::reference_wrapper<const VFP2Matcher<V>>>(*iter) : std::nullopt;
}

} // namespace Dynarmic::A32
