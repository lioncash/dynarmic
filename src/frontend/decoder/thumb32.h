/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <vector>

#include <boost/optional.hpp>

#include "common/common_types.h"
#include "frontend/decoder/decoder_detail.h"
#include "frontend/decoder/matcher.h"

namespace Dynarmic {
namespace Arm {

template <typename Visitor>
using Thumb32Matcher = Matcher<Visitor, u32>;

template<typename V>
boost::optional<const Thumb32Matcher<V>&> DecodeThumb32(u32 instruction) {
    static const std::vector<Thumb32Matcher<V>> table = {

#define INST(fn, name, bitstring) detail::detail<Thumb32Matcher<V>>::GetMatcher(fn, name, bitstring)

        // Branch instructions
        INST(&V::thumb32_BL_imm,         "BL (imm)",                 "11110vvvvvvvvvvv11111vvvvvvvvvvv"), // v4T
        INST(&V::thumb32_BLX_imm,        "BLX (imm)",                "11110vvvvvvvvvvv11101vvvvvvvvvvv"), // v5T

        // Misc instructions
        INST(&V::thumb32_UDF,            "UDF",                      "111101111111----1010------------"), // v6T2

#undef INST

    };

    const auto matches_instruction = [instruction](const auto& matcher){ return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? boost::optional<const Thumb32Matcher<V>&>(*iter) : boost::none;
}

} // namespace Arm
} // namespace Dynarmic
