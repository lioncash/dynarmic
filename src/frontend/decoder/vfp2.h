/* This file is part of the dynarmic project.
 * Copyright (c) 2032 MerryMage
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
using VFP2Matcher = Matcher<Visitor, u32>;

template<typename V>
boost::optional<const VFP2Matcher<V>&> DecodeVFP2(u32 instruction) {
    static const std::vector<VFP2Matcher<V>> table = {

#define INST(fn, name, bitstring) detail::detail<VFP2Matcher<V>>::GetMatcher(fn, name, bitstring)

    // cccc1110________----101-__-0----

    // Floating-point three-register data processing instructions
    INST(&V::vfp2_VMLA,           "VMLA",                    "cccc11100D00nnnndddd101zN0M0mmmm"),
    INST(&V::vfp2_VMLS,           "VMLS",                    "cccc11100D00nnnndddd101zN1M0mmmm"),
    INST(&V::vfp2_VNMLS,          "VNMLS",                   "cccc11100D01nnnndddd101zN0M0mmmm"),
    INST(&V::vfp2_VNMLA,          "VNMLA",                   "cccc11100D01nnnndddd101zN1M0mmmm"),
    INST(&V::vfp2_VMUL,           "VMUL",                    "cccc11100D10nnnndddd101zN0M0mmmm"),
    INST(&V::vfp2_VNMUL,          "VNMUL",                   "cccc11100D10nnnndddd101zN1M0mmmm"),
    INST(&V::vfp2_VADD,           "VADD",                    "cccc11100D11nnnndddd101zN0M0mmmm"),
    INST(&V::vfp2_VSUB,           "VSUB",                    "cccc11100D11nnnndddd101zN1M0mmmm"),
    INST(&V::vfp2_VDIV,           "VDIV",                    "cccc11101D00nnnndddd101zN0M0mmmm"),

    // Floating-point move instructions
    INST(&V::vfp2_VMOV_u32_f64,   "VMOV (core to f64)",      "cccc11100000ddddtttt1011D0010000"),
    INST(&V::vfp2_VMOV_f64_u32,   "VMOV (f64 to core)",      "cccc11100001nnnntttt1011N0010000"),
    INST(&V::vfp2_VMOV_u32_f32,   "VMOV (core to f32)",      "cccc11100000nnnntttt1010N0010000"),
    INST(&V::vfp2_VMOV_f32_u32,   "VMOV (f32 to core)",      "cccc11100001nnnntttt1010N0010000"),
    INST(&V::vfp2_VMOV_2u32_2f32, "VMOV (2xcore to 2xf32)",  "cccc11000100uuuutttt101000M1mmmm"),
    INST(&V::vfp2_VMOV_2f32_2u32, "VMOV (2xf32 to 2xcore)",  "cccc11000101uuuutttt101000M1mmmm"),
    INST(&V::vfp2_VMOV_2u32_f64,  "VMOV (2xcore to f64)",    "cccc11000100uuuutttt101100M1mmmm"),
    INST(&V::vfp2_VMOV_f64_2u32,  "VMOV (f64 to 2xcore)",    "cccc11000101uuuutttt101100M1mmmm"),
    INST(&V::vfp2_VMOV_reg,       "VMOV (reg)",              "cccc11101D110000dddd101z01M0mmmm"),

    // Floating-point other instructions
    INST(&V::vfp2_VABS,           "VABS",                    "cccc11101D110000dddd101z11M0mmmm"),
    INST(&V::vfp2_VNEG,           "VNEG",                    "cccc11101D110001dddd101z01M0mmmm"),
    INST(&V::vfp2_VSQRT,          "VSQRT",                   "cccc11101D110001dddd101z11M0mmmm"),
    INST(&V::vfp2_VCVT_f_to_f,    "VCVT (f32<->f64)",        "cccc11101D110111dddd101z11M0mmmm"),
    INST(&V::vfp2_VCVT_to_float,  "VCVT (to float)",         "cccc11101D111000dddd101zs1M0mmmm"),
    INST(&V::vfp2_VCVT_to_u32,    "VCVT (to u32)",           "cccc11101D111100dddd101zr1M0mmmm"),
    INST(&V::vfp2_VCVT_to_s32,    "VCVT (to s32)",           "cccc11101D111101dddd101zr1M0mmmm"),
    INST(&V::vfp2_VCMP,           "VCMP",                    "cccc11101D110100dddd101zE1M0mmmm"),
    INST(&V::vfp2_VCMP_zero,      "VCMP (with zero)",        "cccc11101D110101dddd101zE1000000"),

    // Floating-point system register access
    INST(&V::vfp2_VMSR,           "VMSR",                    "cccc111011100001tttt101000010000"),
    INST(&V::vfp2_VMRS,           "VMRS",                    "cccc111011110001tttt101000010000"),

    // Extension register load-store instructions
    INST(&V::vfp2_VPUSH,          "VPUSH",                   "cccc11010D101101dddd101zvvvvvvvv"),
    INST(&V::vfp2_VPOP,           "VPOP",                    "cccc11001D111101dddd101zvvvvvvvv"),
    INST(&V::vfp2_VLDR,           "VLDR",                    "cccc1101UD01nnnndddd101zvvvvvvvv"),
    INST(&V::vfp2_VSTR,           "VSTR",                    "cccc1101UD00nnnndddd101zvvvvvvvv"),
    INST(&V::vfp2_VSTM_a1,        "VSTM (A1)",               "cccc110puDw0nnnndddd1011vvvvvvvv"),
    INST(&V::vfp2_VSTM_a2,        "VSTM (A2)",               "cccc110puDw0nnnndddd1010vvvvvvvv"),
    INST(&V::vfp2_VLDM_a1,        "VLDM (A1)",               "cccc110puDw1nnnndddd1011vvvvvvvv"),
    INST(&V::vfp2_VLDM_a2,        "VLDM (A2)",               "cccc110puDw1nnnndddd1010vvvvvvvv"),

#undef INST

    };

    if ((instruction & 0xF0000000) == 0xF0000000)
        return boost::none; // Don't try matching any unconditional instructions.

    const auto matches_instruction = [instruction](const auto& matcher){ return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? boost::optional<const VFP2Matcher<V>&>(*iter) : boost::none;
}

} // namespace Arm
} // namespace Dynarmic
