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

namespace Dynarmic {
namespace Arm {

template <typename Visitor>
using ArmMatcher = Matcher<Visitor, u32>;

template <typename V>
std::vector<ArmMatcher<V>> GetArmDecodeTable() {
    std::vector<ArmMatcher<V>> table = {

#define INST(fn, name, bitstring) detail::detail<ArmMatcher<V>>::GetMatcher(fn, name, bitstring)

        // Branch instructions
        INST(&V::arm_BLX_imm,     "BLX (imm)",           "1111101hvvvvvvvvvvvvvvvvvvvvvvvv"), // v5
        INST(&V::arm_BLX_reg,     "BLX (reg)",           "cccc000100101111111111110011mmmm"), // v5
        INST(&V::arm_B,           "B",                   "cccc1010vvvvvvvvvvvvvvvvvvvvvvvv"), // all
        INST(&V::arm_BL,          "BL",                  "cccc1011vvvvvvvvvvvvvvvvvvvvvvvv"), // all
        INST(&V::arm_BX,          "BX",                  "cccc000100101111111111110001mmmm"), // v4T
        INST(&V::arm_BXJ,         "BXJ",                 "cccc000100101111111111110010mmmm"), // v5J

        // Coprocessor instructions
        INST(&V::arm_CDP,         "CDP",                 "cccc1110ooooNNNNDDDDppppooo0MMMM"), // v2  (CDP2:  v5)
        INST(&V::arm_LDC,         "LDC",                 "cccc110pudw1nnnnDDDDppppvvvvvvvv"), // v2  (LDC2:  v5)
        INST(&V::arm_MCR,         "MCR",                 "cccc1110ooo0NNNNttttppppooo1MMMM"), // v2  (MCR2:  v5)
        INST(&V::arm_MCRR,        "MCRR",                "cccc11000100uuuuttttppppooooMMMM"), // v5E (MCRR2: v6)
        INST(&V::arm_MRC,         "MRC",                 "cccc1110ooo1NNNNttttppppooo1MMMM"), // v2  (MRC2:  v5)
        INST(&V::arm_MRRC,        "MRRC",                "cccc11000101uuuuttttppppooooMMMM"), // v5E (MRRC2: v6)
        INST(&V::arm_STC,         "STC",                 "cccc110pudw0nnnnDDDDppppvvvvvvvv"), // v2  (STC2:  v5)

        // Data Processing instructions
        INST(&V::arm_ADC_imm,     "ADC (imm)",           "cccc0010101Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_ADC_reg,     "ADC (reg)",           "cccc0000101Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_ADC_rsr,     "ADC (rsr)",           "cccc0000101Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_ADD_imm,     "ADD (imm)",           "cccc0010100Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_ADD_reg,     "ADD (reg)",           "cccc0000100Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_ADD_rsr,     "ADD (rsr)",           "cccc0000100Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_AND_imm,     "AND (imm)",           "cccc0010000Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_AND_reg,     "AND (reg)",           "cccc0000000Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_AND_rsr,     "AND (rsr)",           "cccc0000000Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_BIC_imm,     "BIC (imm)",           "cccc0011110Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_BIC_reg,     "BIC (reg)",           "cccc0001110Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_BIC_rsr,     "BIC (rsr)",           "cccc0001110Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_CMN_imm,     "CMN (imm)",           "cccc00110111nnnn0000rrrrvvvvvvvv"), // all
        INST(&V::arm_CMN_reg,     "CMN (reg)",           "cccc00010111nnnn0000vvvvvrr0mmmm"), // all
        INST(&V::arm_CMN_rsr,     "CMN (rsr)",           "cccc00010111nnnn0000ssss0rr1mmmm"), // all
        INST(&V::arm_CMP_imm,     "CMP (imm)",           "cccc00110101nnnn0000rrrrvvvvvvvv"), // all
        INST(&V::arm_CMP_reg,     "CMP (reg)",           "cccc00010101nnnn0000vvvvvrr0mmmm"), // all
        INST(&V::arm_CMP_rsr,     "CMP (rsr)",           "cccc00010101nnnn0000ssss0rr1mmmm"), // all
        INST(&V::arm_EOR_imm,     "EOR (imm)",           "cccc0010001Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_EOR_reg,     "EOR (reg)",           "cccc0000001Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_EOR_rsr,     "EOR (rsr)",           "cccc0000001Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_MOV_imm,     "MOV (imm)",           "cccc0011101S0000ddddrrrrvvvvvvvv"), // all
        INST(&V::arm_MOV_reg,     "MOV (reg)",           "cccc0001101S0000ddddvvvvvrr0mmmm"), // all
        INST(&V::arm_MOV_rsr,     "MOV (rsr)",           "cccc0001101S0000ddddssss0rr1mmmm"), // all
        INST(&V::arm_MVN_imm,     "MVN (imm)",           "cccc0011111S0000ddddrrrrvvvvvvvv"), // all
        INST(&V::arm_MVN_reg,     "MVN (reg)",           "cccc0001111S0000ddddvvvvvrr0mmmm"), // all
        INST(&V::arm_MVN_rsr,     "MVN (rsr)",           "cccc0001111S0000ddddssss0rr1mmmm"), // all
        INST(&V::arm_ORR_imm,     "ORR (imm)",           "cccc0011100Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_ORR_reg,     "ORR (reg)",           "cccc0001100Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_ORR_rsr,     "ORR (rsr)",           "cccc0001100Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_RSB_imm,     "RSB (imm)",           "cccc0010011Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_RSB_reg,     "RSB (reg)",           "cccc0000011Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_RSB_rsr,     "RSB (rsr)",           "cccc0000011Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_RSC_imm,     "RSC (imm)",           "cccc0010111Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_RSC_reg,     "RSC (reg)",           "cccc0000111Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_RSC_rsr,     "RSC (rsr)",           "cccc0000111Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_SBC_imm,     "SBC (imm)",           "cccc0010110Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_SBC_reg,     "SBC (reg)",           "cccc0000110Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_SBC_rsr,     "SBC (rsr)",           "cccc0000110Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_SUB_imm,     "SUB (imm)",           "cccc0010010Snnnnddddrrrrvvvvvvvv"), // all
        INST(&V::arm_SUB_reg,     "SUB (reg)",           "cccc0000010Snnnnddddvvvvvrr0mmmm"), // all
        INST(&V::arm_SUB_rsr,     "SUB (rsr)",           "cccc0000010Snnnnddddssss0rr1mmmm"), // all
        INST(&V::arm_TEQ_imm,     "TEQ (imm)",           "cccc00110011nnnn0000rrrrvvvvvvvv"), // all
        INST(&V::arm_TEQ_reg,     "TEQ (reg)",           "cccc00010011nnnn0000vvvvvrr0mmmm"), // all
        INST(&V::arm_TEQ_rsr,     "TEQ (rsr)",           "cccc00010011nnnn0000ssss0rr1mmmm"), // all
        INST(&V::arm_TST_imm,     "TST (imm)",           "cccc00110001nnnn0000rrrrvvvvvvvv"), // all
        INST(&V::arm_TST_reg,     "TST (reg)",           "cccc00010001nnnn0000vvvvvrr0mmmm"), // all
        INST(&V::arm_TST_rsr,     "TST (rsr)",           "cccc00010001nnnn0000ssss0rr1mmmm"), // all

        // Exception Generating instructions
        INST(&V::arm_BKPT,        "BKPT",                "cccc00010010vvvvvvvvvvvv0111vvvv"), // v5
        INST(&V::arm_SVC,         "SVC",                 "cccc1111vvvvvvvvvvvvvvvvvvvvvvvv"), // all
        INST(&V::arm_UDF,         "UDF",                 "111001111111------------1111----"), // all

        // Extension instructions
        INST(&V::arm_SXTB,        "SXTB",                "cccc011010101111ddddrr000111mmmm"), // v6
        INST(&V::arm_SXTB16,      "SXTB16",              "cccc011010001111ddddrr000111mmmm"), // v6
        INST(&V::arm_SXTH,        "SXTH",                "cccc011010111111ddddrr000111mmmm"), // v6
        INST(&V::arm_SXTAB,       "SXTAB",               "cccc01101010nnnnddddrr000111mmmm"), // v6
        INST(&V::arm_SXTAB16,     "SXTAB16",             "cccc01101000nnnnddddrr000111mmmm"), // v6
        INST(&V::arm_SXTAH,       "SXTAH",               "cccc01101011nnnnddddrr000111mmmm"), // v6
        INST(&V::arm_UXTB,        "UXTB",                "cccc011011101111ddddrr000111mmmm"), // v6
        INST(&V::arm_UXTB16,      "UXTB16",              "cccc011011001111ddddrr000111mmmm"), // v6
        INST(&V::arm_UXTH,        "UXTH",                "cccc011011111111ddddrr000111mmmm"), // v6
        INST(&V::arm_UXTAB,       "UXTAB",               "cccc01101110nnnnddddrr000111mmmm"), // v6
        INST(&V::arm_UXTAB16,     "UXTAB16",             "cccc01101100nnnnddddrr000111mmmm"), // v6
        INST(&V::arm_UXTAH,       "UXTAH",               "cccc01101111nnnnddddrr000111mmmm"), // v6

        // Hint instructions
        INST(&V::arm_PLD,         "PLD",                 "111101-1-101----1111------------"), // v5E; different on v7
        INST(&V::arm_SEV,         "SEV",                 "----0011001000001111000000000100"), // v6K
        INST(&V::arm_WFE,         "WFE",                 "----0011001000001111000000000010"), // v6K
        INST(&V::arm_WFI,         "WFI",                 "----0011001000001111000000000011"), // v6K
        INST(&V::arm_YIELD,       "YIELD",               "----0011001000001111000000000001"), // v6K

        // Synchronization Primitive instructions
        INST(&V::arm_CLREX,       "CLREX",               "11110101011111111111000000011111"), // v6K
        INST(&V::arm_LDREX,       "LDREX",               "cccc00011001nnnndddd111110011111"), // v6
        INST(&V::arm_LDREXB,      "LDREXB",              "cccc00011101nnnndddd111110011111"), // v6K
        INST(&V::arm_LDREXD,      "LDREXD",              "cccc00011011nnnndddd111110011111"), // v6K
        INST(&V::arm_LDREXH,      "LDREXH",              "cccc00011111nnnndddd111110011111"), // v6K
        INST(&V::arm_STREX,       "STREX",               "cccc00011000nnnndddd11111001mmmm"), // v6
        INST(&V::arm_STREXB,      "STREXB",              "cccc00011100nnnndddd11111001mmmm"), // v6K
        INST(&V::arm_STREXD,      "STREXD",              "cccc00011010nnnndddd11111001mmmm"), // v6K
        INST(&V::arm_STREXH,      "STREXH",              "cccc00011110nnnndddd11111001mmmm"), // v6K
        INST(&V::arm_SWP,         "SWP",                 "cccc00010000nnnntttt00001001uuuu"), // v2S (v6: Deprecated)
        INST(&V::arm_SWPB,        "SWPB",                "cccc00010100nnnntttt00001001uuuu"), // v2S (v6: Deprecated)

        // Load/Store instructions
        INST(&V::arm_LDRBT,       "LDRBT (A1)",          "----0100-111--------------------"),
        INST(&V::arm_LDRBT,       "LDRBT (A2)",          "----0110-111---------------0----"),
        INST(&V::arm_LDRHT,       "LDRHT (A1)",          "----0000-111------------1011----"),
        INST(&V::arm_LDRHT,       "LDRHT (A2)",          "----0000-011--------00001011----"),
        INST(&V::arm_LDRSBT,      "LDRSBT (A1)",         "----0000-111------------1101----"),
        INST(&V::arm_LDRSBT,      "LDRSBT (A2)",         "----0000-011--------00001101----"),
        INST(&V::arm_LDRSHT,      "LDRSHT (A1)",         "----0000-111------------1111----"),
        INST(&V::arm_LDRSHT,      "LDRSHT (A2)",         "----0000-011--------00001111----"),
        INST(&V::arm_LDRT,        "LDRT (A1)",           "----0100-011--------------------"),
        INST(&V::arm_LDRT,        "LDRT (A2)",           "----0110-011---------------0----"),
        INST(&V::arm_STRBT,       "STRBT (A1)",          "----0100-110--------------------"),
        INST(&V::arm_STRBT,       "STRBT (A2)",          "----0110-110---------------0----"),
        INST(&V::arm_STRHT,       "STRHT (A1)",          "----0000-110------------1011----"),
        INST(&V::arm_STRHT,       "STRHT (A2)",          "----0000-010--------00001011----"),
        INST(&V::arm_STRT,        "STRT (A1)",           "----0100-010--------------------"),
        INST(&V::arm_STRT,        "STRT (A2)",           "----0110-010---------------0----"),
        INST(&V::arm_LDR_lit,     "LDR (lit)",           "cccc0101u0011111ttttvvvvvvvvvvvv"),
        INST(&V::arm_LDR_imm,     "LDR (imm)",           "cccc010pu0w1nnnnttttvvvvvvvvvvvv"),
        INST(&V::arm_LDR_reg,     "LDR (reg)",           "cccc011pu0w1nnnnttttvvvvvrr0mmmm"),
        INST(&V::arm_LDRB_lit,    "LDRB (lit)",          "cccc0101u1011111ttttvvvvvvvvvvvv"),
        INST(&V::arm_LDRB_imm,    "LDRB (imm)",          "cccc010pu1w1nnnnttttvvvvvvvvvvvv"),
        INST(&V::arm_LDRB_reg,    "LDRB (reg)",          "cccc011pu1w1nnnnttttvvvvvrr0mmmm"),
        INST(&V::arm_LDRD_lit,    "LDRD (lit)",          "cccc0001u1001111ttttvvvv1101vvvv"),
        INST(&V::arm_LDRD_imm,    "LDRD (imm)",          "cccc000pu1w0nnnnttttvvvv1101vvvv"), // v5E
        INST(&V::arm_LDRD_reg,    "LDRD (reg)",          "cccc000pu0w0nnnntttt00001101mmmm"), // v5E
        INST(&V::arm_LDRH_lit,    "LDRH (lit)",          "cccc000pu1w11111ttttvvvv1011vvvv"),
        INST(&V::arm_LDRH_imm,    "LDRH (imm)",          "cccc000pu1w1nnnnttttvvvv1011vvvv"),
        INST(&V::arm_LDRH_reg,    "LDRH (reg)",          "cccc000pu0w1nnnntttt00001011mmmm"),
        INST(&V::arm_LDRSB_lit,   "LDRSB (lit)",         "cccc0001u1011111ttttvvvv1101vvvv"),
        INST(&V::arm_LDRSB_imm,   "LDRSB (imm)",         "cccc000pu1w1nnnnttttvvvv1101vvvv"),
        INST(&V::arm_LDRSB_reg,   "LDRSB (reg)",         "cccc000pu0w1nnnntttt00001101mmmm"),
        INST(&V::arm_LDRSH_lit,   "LDRSH (lit)",         "cccc0001u1011111ttttvvvv1111vvvv"),
        INST(&V::arm_LDRSH_imm,   "LDRSH (imm)",         "cccc000pu1w1nnnnttttvvvv1111vvvv"),
        INST(&V::arm_LDRSH_reg,   "LDRSH (reg)",         "cccc000pu0w1nnnntttt00001111mmmm"),
        INST(&V::arm_STR_imm,     "STR (imm)",           "cccc010pu0w0nnnnttttvvvvvvvvvvvv"),
        INST(&V::arm_STR_reg,     "STR (reg)",           "cccc011pu0w0nnnnttttvvvvvrr0mmmm"),
        INST(&V::arm_STRB_imm,    "STRB (imm)",          "cccc010pu1w0nnnnttttvvvvvvvvvvvv"),
        INST(&V::arm_STRB_reg,    "STRB (reg)",          "cccc011pu1w0nnnnttttvvvvvrr0mmmm"),
        INST(&V::arm_STRD_imm,    "STRD (imm)",          "cccc000pu1w0nnnnttttvvvv1111vvvv"), // v5E
        INST(&V::arm_STRD_reg,    "STRD (reg)",          "cccc000pu0w0nnnntttt00001111mmmm"), // v5E
        INST(&V::arm_STRH_imm,    "STRH (imm)",          "cccc000pu1w0nnnnttttvvvv1011vvvv"),
        INST(&V::arm_STRH_reg,    "STRH (reg)",          "cccc000pu0w0nnnntttt00001011mmmm"),

        // Load/Store Multiple instructions
        INST(&V::arm_LDM,         "LDM",                 "cccc100010w1nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_LDMDA,       "LDMDA",               "cccc100000w1nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_LDMDB,       "LDMDB",               "cccc100100w1nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_LDMIB,       "LDMIB",               "cccc100110w1nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_LDM_usr,     "LDM (usr reg)",       "----100--101--------------------"), // all
        INST(&V::arm_LDM_eret,    "LDM (exce ret)",      "----100--1-1----1---------------"), // all
        INST(&V::arm_STM,         "STM",                 "cccc100010w0nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_STMDA,       "STMDA",               "cccc100000w0nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_STMDB,       "STMDB",               "cccc100100w0nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_STMIB,       "STMIB",               "cccc100110w0nnnnxxxxxxxxxxxxxxxx"), // all
        INST(&V::arm_STM_usr,     "STM (usr reg)",       "----100--100--------------------"), // all

        // Miscellaneous instructions
        INST(&V::arm_CLZ,         "CLZ",                 "cccc000101101111dddd11110001mmmm"), // v5
        INST(&V::arm_NOP,         "NOP",                 "----0011001000001111000000000000"), // v6K
        INST(&V::arm_SEL,         "SEL",                 "cccc01101000nnnndddd11111011mmmm"), // v6

        // Unsigned Sum of Absolute Differences instructions
        INST(&V::arm_USAD8,       "USAD8",               "cccc01111000dddd1111mmmm0001nnnn"), // v6
        INST(&V::arm_USADA8,      "USADA8",              "cccc01111000ddddaaaammmm0001nnnn"), // v6

        // Packing instructions
        INST(&V::arm_PKHBT,       "PKHBT",               "cccc01101000nnnnddddvvvvv001mmmm"), // v6K
        INST(&V::arm_PKHTB,       "PKHTB",               "cccc01101000nnnnddddvvvvv101mmmm"), // v6K

        // Reversal instructions
        INST(&V::arm_REV,         "REV",                 "cccc011010111111dddd11110011mmmm"), // v6
        INST(&V::arm_REV16,       "REV16",               "cccc011010111111dddd11111011mmmm"), // v6
        INST(&V::arm_REVSH,       "REVSH",               "cccc011011111111dddd11111011mmmm"), // v6

        // Saturation instructions
        INST(&V::arm_SSAT,        "SSAT",                "cccc0110101vvvvvddddvvvvvr01nnnn"), // v6
        INST(&V::arm_SSAT16,      "SSAT16",              "cccc01101010vvvvdddd11110011nnnn"), // v6
        INST(&V::arm_USAT,        "USAT",                "cccc0110111vvvvvddddvvvvvr01nnnn"), // v6
        INST(&V::arm_USAT16,      "USAT16",              "cccc01101110vvvvdddd11110011nnnn"), // v6

        // Multiply (Normal) instructions
        INST(&V::arm_MLA,         "MLA",                 "cccc0000001Sddddaaaammmm1001nnnn"), // v2
        INST(&V::arm_MUL,         "MUL",                 "cccc0000000Sdddd0000mmmm1001nnnn"), // v2

        // Multiply (Long) instructions
        INST(&V::arm_SMLAL,       "SMLAL",               "cccc0000111Sddddaaaammmm1001nnnn"), // v3M
        INST(&V::arm_SMULL,       "SMULL",               "cccc0000110Sddddaaaammmm1001nnnn"), // v3M
        INST(&V::arm_UMAAL,       "UMAAL",               "cccc00000100ddddaaaammmm1001nnnn"), // v6
        INST(&V::arm_UMLAL,       "UMLAL",               "cccc0000101Sddddaaaammmm1001nnnn"), // v3M
        INST(&V::arm_UMULL,       "UMULL",               "cccc0000100Sddddaaaammmm1001nnnn"), // v3M

        // Multiply (Halfword) instructions
        INST(&V::arm_SMLALxy,     "SMLALXY",             "cccc00010100ddddaaaammmm1xy0nnnn"), // v5xP
        INST(&V::arm_SMLAxy,      "SMLAXY",              "cccc00010000ddddaaaammmm1xy0nnnn"), // v5xP
        INST(&V::arm_SMULxy,      "SMULXY",              "cccc00010110dddd0000mmmm1xy0nnnn"), // v5xP

        // Multiply (Word by Halfword) instructions
        INST(&V::arm_SMLAWy,      "SMLAWY",              "cccc00010010ddddaaaammmm1y00nnnn"), // v5xP
        INST(&V::arm_SMULWy,      "SMULWY",              "cccc00010010dddd0000mmmm1y10nnnn"), // v5xP

        // Multiply (Most Significant Word) instructions
        INST(&V::arm_SMMUL,       "SMMUL",               "cccc01110101dddd1111mmmm00R1nnnn"), // v6
        INST(&V::arm_SMMLA,       "SMMLA",               "cccc01110101ddddaaaammmm00R1nnnn"), // v6
        INST(&V::arm_SMMLS,       "SMMLS",               "cccc01110101ddddaaaammmm11R1nnnn"), // v6

        // Multiply (Dual) instructions
        INST(&V::arm_SMLAD,       "SMLAD",               "cccc01110000ddddaaaammmm00M1nnnn"), // v6
        INST(&V::arm_SMLALD,      "SMLALD",              "cccc01110100ddddaaaammmm00M1nnnn"), // v6
        INST(&V::arm_SMLSD,       "SMLSD",               "cccc01110000ddddaaaammmm01M1nnnn"), // v6
        INST(&V::arm_SMLSLD,      "SMLSLD",              "cccc01110100ddddaaaammmm01M1nnnn"), // v6
        INST(&V::arm_SMUAD,       "SMUAD",               "cccc01110000dddd1111mmmm00M1nnnn"), // v6
        INST(&V::arm_SMUSD,       "SMUSD",               "cccc01110000dddd1111mmmm01M1nnnn"), // v6

        // Parallel Add/Subtract (Modulo) instructions
        INST(&V::arm_SADD8,       "SADD8",               "cccc01100001nnnndddd11111001mmmm"), // v6
        INST(&V::arm_SADD16,      "SADD16",              "cccc01100001nnnndddd11110001mmmm"), // v6
        INST(&V::arm_SASX,        "SASX",                "cccc01100001nnnndddd11110011mmmm"), // v6
        INST(&V::arm_SSAX,        "SSAX",                "cccc01100001nnnndddd11110101mmmm"), // v6
        INST(&V::arm_SSUB8,       "SSUB8",               "cccc01100001nnnndddd11111111mmmm"), // v6
        INST(&V::arm_SSUB16,      "SSUB16",              "cccc01100001nnnndddd11110111mmmm"), // v6
        INST(&V::arm_UADD8,       "UADD8",               "cccc01100101nnnndddd11111001mmmm"), // v6
        INST(&V::arm_UADD16,      "UADD16",              "cccc01100101nnnndddd11110001mmmm"), // v6
        INST(&V::arm_UASX,        "UASX",                "cccc01100101nnnndddd11110011mmmm"), // v6
        INST(&V::arm_USAX,        "USAX",                "cccc01100101nnnndddd11110101mmmm"), // v6
        INST(&V::arm_USUB8,       "USUB8",               "cccc01100101nnnndddd11111111mmmm"), // v6
        INST(&V::arm_USUB16,      "USUB16",              "cccc01100101nnnndddd11110111mmmm"), // v6

        // Parallel Add/Subtract (Saturating) instructions
        INST(&V::arm_QADD8,       "QADD8",               "cccc01100010nnnndddd11111001mmmm"), // v6
        INST(&V::arm_QADD16,      "QADD16",              "cccc01100010nnnndddd11110001mmmm"), // v6
        INST(&V::arm_QASX,        "QASX",                "cccc01100010nnnndddd11110011mmmm"), // v6
        INST(&V::arm_QSAX,        "QSAX",                "cccc01100010nnnndddd11110101mmmm"), // v6
        INST(&V::arm_QSUB8,       "QSUB8",               "cccc01100010nnnndddd11111111mmmm"), // v6
        INST(&V::arm_QSUB16,      "QSUB16",              "cccc01100010nnnndddd11110111mmmm"), // v6
        INST(&V::arm_UQADD8,      "UQADD8",              "cccc01100110nnnndddd11111001mmmm"), // v6
        INST(&V::arm_UQADD16,     "UQADD16",             "cccc01100110nnnndddd11110001mmmm"), // v6
        INST(&V::arm_UQASX,       "UQASX",               "cccc01100110nnnndddd11110011mmmm"), // v6
        INST(&V::arm_UQSAX,       "UQSAX",               "cccc01100110nnnndddd11110101mmmm"), // v6
        INST(&V::arm_UQSUB8,      "UQSUB8",              "cccc01100110nnnndddd11111111mmmm"), // v6
        INST(&V::arm_UQSUB16,     "UQSUB16",             "cccc01100110nnnndddd11110111mmmm"), // v6

        // Parallel Add/Subtract (Halving) instructions
        INST(&V::arm_SHADD8,      "SHADD8",              "cccc01100011nnnndddd11111001mmmm"), // v6
        INST(&V::arm_SHADD16,     "SHADD16",             "cccc01100011nnnndddd11110001mmmm"), // v6
        INST(&V::arm_SHASX,       "SHASX",               "cccc01100011nnnndddd11110011mmmm"), // v6
        INST(&V::arm_SHSAX,       "SHSAX",               "cccc01100011nnnndddd11110101mmmm"), // v6
        INST(&V::arm_SHSUB8,      "SHSUB8",              "cccc01100011nnnndddd11111111mmmm"), // v6
        INST(&V::arm_SHSUB16,     "SHSUB16",             "cccc01100011nnnndddd11110111mmmm"), // v6
        INST(&V::arm_UHADD8,      "UHADD8",              "cccc01100111nnnndddd11111001mmmm"), // v6
        INST(&V::arm_UHADD16,     "UHADD16",             "cccc01100111nnnndddd11110001mmmm"), // v6
        INST(&V::arm_UHASX,       "UHASX",               "cccc01100111nnnndddd11110011mmmm"), // v6
        INST(&V::arm_UHSAX,       "UHSAX",               "cccc01100111nnnndddd11110101mmmm"), // v6
        INST(&V::arm_UHSUB8,      "UHSUB8",              "cccc01100111nnnndddd11111111mmmm"), // v6
        INST(&V::arm_UHSUB16,     "UHSUB16",             "cccc01100111nnnndddd11110111mmmm"), // v6

        // Saturated Add/Subtract instructions
        INST(&V::arm_QADD,        "QADD",                "cccc00010000nnnndddd00000101mmmm"), // v5xP
        INST(&V::arm_QSUB,        "QSUB",                "cccc00010010nnnndddd00000101mmmm"), // v5xP
        INST(&V::arm_QDADD,       "QDADD",               "cccc00010100nnnndddd00000101mmmm"), // v5xP
        INST(&V::arm_QDSUB,       "QDSUB",               "cccc00010110nnnndddd00000101mmmm"), // v5xP

        // Status Register Access instructions
        INST(&V::arm_CPS,         "CPS",                 "111100010000---00000000---0-----"), // v6
        INST(&V::arm_SETEND,      "SETEND",              "1111000100000001000000e000000000"), // v6
        INST(&V::arm_MRS,         "MRS",                 "cccc000100001111dddd000000000000"), // v3
        INST(&V::arm_MSR_imm,     "MSR (imm)",           "cccc00110010mm001111rrrrvvvvvvvv"), // v3
        INST(&V::arm_MSR_reg,     "MSR (reg)",           "cccc00010010mm00111100000000nnnn"), // v3
        INST(&V::arm_RFE,         "RFE",                 "1111100--0-1----0000101000000000"), // v6
        INST(&V::arm_SRS,         "SRS",                 "0000011--0-00000000000000001----"), // v6

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

} // namespace Arm
} // namespace Dynarmic
