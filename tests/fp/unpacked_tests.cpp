/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <catch.hpp>

#include "common/fp/unpacked.h"
#include "rand_int.h"

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
        {0x3F7FFFFF, {FPType::Nonzero, false, {false, -24, 0xFFFFFF}}, 0}, // 1.0 - epsilon
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

TEST_CASE("FPRound Tests", "[fp]") {
    const static std::vector<std::tuple<u32, std::tuple<FPType, bool, FPUnpacked<u64>>, u32>> test_cases {
        {0x7F800000, {FPType::Infinity, false, {false, 1000000, 1}}, 0x14},
        {0xFF800000, {FPType::Infinity, true, {true, 1000000, 1}}, 0x14},
        {0x00000001, {FPType::Nonzero, false, {false, -149, 1}}, 0}, // Smallest single precision denormal is 2^-149.
        {0x3F7FFFFF, {FPType::Nonzero, false, {false, -24, 0xFFFFFF}}, 0}, // 1.0 - epsilon
        {0x3F800000, {FPType::Nonzero, false, {false, -28, 0xFFFFFFF}}, 0x10}, // rounds to 1.0
    };

    const FPCR fpcr;
    for (const auto& [expected_output, input, expected_fpsr] : test_cases) {
        FPSR fpsr;
        const auto output = FPRound<u32>(std::get<2>(input), fpcr, fpsr);

        INFO("Expected Output: " << std::hex << expected_output);
        REQUIRE(output == expected_output);
        REQUIRE(fpsr.Value() == expected_fpsr);
    }
}

TEST_CASE("FPUnpack<->FPRound Round-trip Tests", "[fp]") {
    const FPCR fpcr;
    for (size_t count = 0; count < 100000; count++) {
        FPSR fpsr;
        const u32 input = RandInt(0, 1) == 0 ? RandInt<u32>(0x00000001, 0x7F800000) : RandInt<u32>(0x80000001, 0xFF800000);
        const auto intermediate = std::get<2>(FPUnpack<u32>(input, fpcr, fpsr));
        const u32 output = FPRound<u32>(intermediate, fpcr, fpsr);

        INFO("Count: " << count);
        INFO("Intermediate Values: " << std::hex << intermediate.sign << ';' << intermediate.exponent << ';' << intermediate.mantissa);
        REQUIRE(input == output);
    }
}
