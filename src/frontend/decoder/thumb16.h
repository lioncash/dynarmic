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
using Thumb16Matcher = Matcher<Visitor, u16>;

template<typename V>
boost::optional<const Thumb16Matcher<V>&> DecodeThumb16(u16 instruction) {
    static const std::vector<Thumb16Matcher<V>> table = {

#define INST(fn, name, bitstring) detail::detail<Thumb16Matcher<V>>::GetMatcher(fn, name, bitstring)

        // Shift (immediate), add, subtract, move and compare instructions
        INST(&V::thumb16_LSL_imm,        "LSL (imm)",                "00000vvvvvmmmddd"),
        INST(&V::thumb16_LSR_imm,        "LSR (imm)",                "00001vvvvvmmmddd"),
        INST(&V::thumb16_ASR_imm,        "ASR (imm)",                "00010vvvvvmmmddd"),
        INST(&V::thumb16_ADD_reg_t1,     "ADD (reg, T1)",            "0001100mmmnnnddd"),
        INST(&V::thumb16_SUB_reg,        "SUB (reg)",                "0001101mmmnnnddd"),
        INST(&V::thumb16_ADD_imm_t1,     "ADD (imm, T1)",            "0001110vvvnnnddd"),
        INST(&V::thumb16_SUB_imm_t1,     "SUB (imm, T1)",            "0001111vvvnnnddd"),
        INST(&V::thumb16_MOV_imm,        "MOV (imm)",                "00100dddvvvvvvvv"),
        INST(&V::thumb16_CMP_imm,        "CMP (imm)",                "00101nnnvvvvvvvv"),
        INST(&V::thumb16_ADD_imm_t2,     "ADD (imm, T2)",            "00110dddvvvvvvvv"),
        INST(&V::thumb16_SUB_imm_t2,     "SUB (imm, T2)",            "00111dddvvvvvvvv"),

         // Data-processing instructions
        INST(&V::thumb16_AND_reg,        "AND (reg)",                "0100000000mmmddd"),
        INST(&V::thumb16_EOR_reg,        "EOR (reg)",                "0100000001mmmddd"),
        INST(&V::thumb16_LSL_reg,        "LSL (reg)",                "0100000010mmmddd"),
        INST(&V::thumb16_LSR_reg,        "LSR (reg)",                "0100000011mmmddd"),
        INST(&V::thumb16_ASR_reg,        "ASR (reg)",                "0100000100mmmddd"),
        INST(&V::thumb16_ADC_reg,        "ADC (reg)",                "0100000101mmmddd"),
        INST(&V::thumb16_SBC_reg,        "SBC (reg)",                "0100000110mmmddd"),
        INST(&V::thumb16_ROR_reg,        "ROR (reg)",                "0100000111sssddd"),
        INST(&V::thumb16_TST_reg,        "TST (reg)",                "0100001000mmmnnn"),
        INST(&V::thumb16_RSB_imm,        "RSB (imm)",                "0100001001nnnddd"),
        INST(&V::thumb16_CMP_reg_t1,     "CMP (reg, T1)",            "0100001010mmmnnn"),
        INST(&V::thumb16_CMN_reg,        "CMN (reg)",                "0100001011mmmnnn"),
        INST(&V::thumb16_ORR_reg,        "ORR (reg)",                "0100001100mmmddd"),
        INST(&V::thumb16_MUL_reg,        "MUL (reg)",                "0100001101nnnddd"),
        INST(&V::thumb16_BIC_reg,        "BIC (reg)",                "0100001110mmmddd"),
        INST(&V::thumb16_MVN_reg,        "MVN (reg)",                "0100001111mmmddd"),

        // Special data instructions
        INST(&V::thumb16_ADD_reg_t2,     "ADD (reg, T2)",            "01000100Dmmmmddd"), // v4T, Low regs: v6T2
        INST(&V::thumb16_CMP_reg_t2,     "CMP (reg, T2)",            "01000101Nmmmmnnn"), // v4T
        INST(&V::thumb16_MOV_reg,        "MOV (reg)",                "01000110Dmmmmddd"), // v4T, Low regs: v6

        // Store/Load single data item instructions
        INST(&V::thumb16_LDR_literal,    "LDR (literal)",            "01001tttvvvvvvvv"),
        INST(&V::thumb16_STR_reg,        "STR (reg)",                "0101000mmmnnnttt"),
        INST(&V::thumb16_STRH_reg,       "STRH (reg)",               "0101001mmmnnnttt"),
        INST(&V::thumb16_STRB_reg,       "STRB (reg)",               "0101010mmmnnnttt"),
        INST(&V::thumb16_LDRSB_reg,      "LDRSB (reg)",              "0101011mmmnnnttt"),
        INST(&V::thumb16_LDR_reg,        "LDR (reg)",                "0101100mmmnnnttt"),
        INST(&V::thumb16_LDRH_reg,       "LDRH (reg)",               "0101101mmmnnnttt"),
        INST(&V::thumb16_LDRB_reg,       "LDRB (reg)",               "0101110mmmnnnttt"),
        INST(&V::thumb16_LDRSH_reg,      "LDRSH (reg)",              "0101111mmmnnnttt"),
        INST(&V::thumb16_STR_imm_t1,     "STR (imm, T1)",            "01100vvvvvnnnttt"),
        INST(&V::thumb16_LDR_imm_t1,     "LDR (imm, T1)",            "01101vvvvvnnnttt"),
        INST(&V::thumb16_STRB_imm,       "STRB (imm)",               "01110vvvvvnnnttt"),
        INST(&V::thumb16_LDRB_imm,       "LDRB (imm)",               "01111vvvvvnnnttt"),
        INST(&V::thumb16_STRH_imm,       "STRH (imm)",               "10000vvvvvnnnttt"),
        INST(&V::thumb16_LDRH_imm,       "LDRH (imm)",               "10001vvvvvnnnttt"),
        INST(&V::thumb16_STR_imm_t2,     "STR (imm, T2)",            "10010tttvvvvvvvv"),
        INST(&V::thumb16_LDR_imm_t2,     "LDR (imm, T2)",            "10011tttvvvvvvvv"),

        // Generate relative address instructions
        INST(&V::thumb16_ADR,            "ADR",                      "10100dddvvvvvvvv"),
        INST(&V::thumb16_ADD_sp_t1,      "ADD (SP plus imm, T1)",    "10101dddvvvvvvvv"),
        INST(&V::thumb16_ADD_sp_t2,      "ADD (SP plus imm, T2)",    "101100000vvvvvvv"), // v4T
        INST(&V::thumb16_SUB_sp,         "SUB (SP minus imm)",       "101100001vvvvvvv"), // v4T

        // Miscellaneous 16-bit instructions
        INST(&V::thumb16_SXTH,           "SXTH",                     "1011001000mmmddd"), // v6
        INST(&V::thumb16_SXTB,           "SXTB",                     "1011001001mmmddd"), // v6
        INST(&V::thumb16_UXTH,           "UXTH",                     "1011001010mmmddd"), // v6
        INST(&V::thumb16_UXTB,           "UXTB",                     "1011001011mmmddd"), // v6
        INST(&V::thumb16_PUSH,           "PUSH",                     "1011010Mxxxxxxxx"), // v4T
        INST(&V::thumb16_POP,            "POP",                      "1011110Pxxxxxxxx"), // v4T
        INST(&V::thumb16_SETEND,         "SETEND",                   "101101100101x000"), // v6
        INST(&V::thumb16_CPS,            "CPS",                      "10110110011m0aif"), // v6
        INST(&V::thumb16_REV,            "REV",                      "1011101000mmmddd"), // v6
        INST(&V::thumb16_REV16,          "REV16",                    "1011101001mmmddd"), // v6
        INST(&V::thumb16_REVSH,          "REVSH",                    "1011101011mmmddd"), // v6
        //INST(&V::thumb16_BKPT,           "BKPT",                     "10111110xxxxxxxx"), // v5

        // Store/Load multiple registers
        INST(&V::thumb16_STMIA,          "STMIA",                    "11000nnnxxxxxxxx"),
        INST(&V::thumb16_LDMIA,          "LDMIA",                    "11001nnnxxxxxxxx"),

        // Branch instructions
        INST(&V::thumb16_BX,             "BX",                       "010001110mmmm000"), // v4T
        INST(&V::thumb16_BLX_reg,        "BLX (reg)",                "010001111mmmm000"), // v5T
        INST(&V::thumb16_UDF,            "UDF",                      "11011110--------"),
        INST(&V::thumb16_SVC,            "SVC",                      "11011111xxxxxxxx"),
        INST(&V::thumb16_B_t1,           "B (T1)",                   "1101ccccvvvvvvvv"),
        INST(&V::thumb16_B_t2,           "B (T2)",                   "11100vvvvvvvvvvv"),

#undef INST

    };

    const auto matches_instruction = [instruction](const auto& matcher){ return matcher.Matches(instruction); };

    auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
    return iter != table.end() ? boost::optional<const Thumb16Matcher<V>&>(*iter) : boost::none;
}

} // namespace Arm
} // namespace Dynarmic
