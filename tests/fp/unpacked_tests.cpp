/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <catch.hpp>

#include "common/fp/unpacked.h"

using namespace Dynarmic;
using namespace Dynarmic::FP;

TEST_CASE("FPUnpack Tests", "[fp]") {
    const static std::vector<std::tuple<u32, std::tuple<FPType, bool, FPUnpacked<u64>>, u32>> test_cases {
        {0x00000000, {FPType::Zero, false, {false, 0, 0}}, 0},
        {0x7F800000, {FPType::Infinity, false, {false, 1000000, 1}}, 0},
        {0xFF800000, {FPType::Infinity, true, {true, 1000000, 1}}, 0},
        {0x7F800001, {FPType::SNaN, false, {false, 0, 0}}, 0},
        {0xFF800001, {FPType::SNaN, true, {true, 0, 0}}, 0},
        {0x7FC00001, {FPType::QNaN, false, {false, 0, 0}}, 0},
        {0xFFC00001, {FPType::QNaN, true, {true, 0, 0}}, 0},
        {0x00000001, {FPType::Nonzero, false, {false, -149, 1}}, 0}, // Smallest single precision denormal is 2^-149.
    };

    const FPCR fpcr;
    for (const auto& [input, expected_output, expected_fpsr] : test_cases) {
        FPSR fpsr;
        const auto output = FPUnpack<u32>(input, fpcr, fpsr);

        INFO("Input: " << std::hex << input);
        REQUIRE(output == expected_output);
        REQUIRE(fpsr.Value() == expected_fpsr);
    }
}
