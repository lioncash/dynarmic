/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstdlib>
#include <string>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "common/bit_util.h"
#include "common/string_util.h"
#include "frontend/A32/decoder/thumb16.h"
#include "frontend/A32/disassembler/disassembler.h"
#include "frontend/A32/types.h"

namespace Dynarmic::A32 {

class DisassemblerVisitor {
public:
    using instruction_return_type = std::string;

    std::string thumb16_LSL_imm(Imm5 imm5, Reg m, Reg d) {
        return fmt::format("lsls {}, {}, #{}", d, m, imm5);
    }

    std::string thumb16_LSR_imm(Imm5 imm5, Reg m, Reg d) {
        return fmt::format("lsrs {}, {}, #{}", d, m, imm5 != 0 ? imm5 : 32);
    }

    std::string thumb16_ASR_imm(Imm5 imm5, Reg m, Reg d) {
        return fmt::format("asrs {}, {}, #{}", d, m, imm5 != 0 ? imm5 : 32);
    }

    std::string thumb16_ADD_reg_t1(Reg m, Reg n, Reg d) {
        return fmt::format("adds {}, {}, {}", d, n, m);
    }

    std::string thumb16_SUB_reg(Reg m, Reg n, Reg d) {
        return fmt::format("subs {}, {}, {}", d, n, m);
    }

    std::string thumb16_ADD_imm_t1(Imm3 imm3, Reg n, Reg d) {
        return fmt::format("adds {}, {}, #{}", d, n, imm3);
    }

    std::string thumb16_SUB_imm_t1(Imm3 imm3, Reg n, Reg d) {
        return fmt::format("subs {}, {}, #{}", d, n, imm3);
    }

    std::string thumb16_MOV_imm(Reg d, Imm8 imm8) {
        return fmt::format("movs {}, #{}", d, imm8);
    }

    std::string thumb16_CMP_imm(Reg n, Imm8 imm8) {
        return fmt::format("cmp {}, #{}", n, imm8);
    }

    std::string thumb16_ADD_imm_t2(Reg d_n, Imm8 imm8) {
        return fmt::format("adds {}, #{}", d_n, imm8);
    }

    std::string thumb16_SUB_imm_t2(Reg d_n, Imm8 imm8) {
        return fmt::format("subs {}, #{}", d_n, imm8);
    }

    std::string thumb16_AND_reg(Reg m, Reg d_n) {
        return fmt::format("ands {}, {}", d_n, m);
    }

    std::string thumb16_EOR_reg(Reg m, Reg d_n) {
        return fmt::format("eors {}, {}", d_n, m);
    }

    std::string thumb16_LSL_reg(Reg m, Reg d_n) {
        return fmt::format("lsls {}, {}", d_n, m);
    }

    std::string thumb16_LSR_reg(Reg m, Reg d_n) {
        return fmt::format("lsrs {}, {}", d_n, m);
    }

    std::string thumb16_ASR_reg(Reg m, Reg d_n) {
        return fmt::format("asrs {}, {}", d_n, m);
    }

    std::string thumb16_ADC_reg(Reg m, Reg d_n) {
        return fmt::format("adcs {}, {}", d_n, m);
    }

    std::string thumb16_SBC_reg(Reg m, Reg d_n) {
        return fmt::format("sbcs {}, {}", d_n, m);
    }

    std::string thumb16_ROR_reg(Reg m, Reg d_n) {
        return fmt::format("rors {}, {}", d_n, m);
    }

    std::string thumb16_TST_reg(Reg m, Reg n) {
        return fmt::format("tst {}, {}", n, m);
    }

    std::string thumb16_RSB_imm(Reg n, Reg d) {
        // Pre-UAL syntax: NEGS <Rd>, <Rn>
        return fmt::format("rsbs {}, {}, #0", d, n);
    }

    std::string thumb16_CMP_reg_t1(Reg m, Reg n) {
        return fmt::format("cmp {}, {}", n, m);
    }

    std::string thumb16_CMN_reg(Reg m, Reg n) {
        return fmt::format("cmn {}, {}", n, m);
    }

    std::string thumb16_ORR_reg(Reg m, Reg d_n) {
        return fmt::format("orrs {}, {}", d_n, m);
    }

    std::string thumb16_MUL_reg(Reg n, Reg d_m) {
        return fmt::format("muls {}, {}, {}", d_m, n, d_m);
    }

    std::string thumb16_BIC_reg(Reg m, Reg d_n) {
        return fmt::format("bics {}, {}", d_n, m);
    }

    std::string thumb16_MVN_reg(Reg m, Reg d) {
        return fmt::format("mvns {}, {}", d, m);
    }

    std::string thumb16_ADD_reg_t2(bool d_n_hi, Reg m, Reg d_n_lo) {
        Reg d_n = d_n_hi ? (d_n_lo + 8) : d_n_lo;
        return fmt::format("add {}, {}", d_n, m);
    }

    std::string thumb16_CMP_reg_t2(bool n_hi, Reg m, Reg n_lo) {
        Reg n = n_hi ? (n_lo + 8) : n_lo;
        return fmt::format("cmp {}, {}", n, m);
    }

    std::string thumb16_MOV_reg(bool d_hi, Reg m, Reg d_lo) {
        Reg d = d_hi ? (d_lo + 8) : d_lo;
        return fmt::format("mov {}, {}", d, m);
    }

    std::string thumb16_LDR_literal(Reg t, Imm8 imm8) {
        u32 imm32 = imm8 << 2;
        return fmt::format("ldr {}, [pc, #{}]", t, imm32);
    }

    std::string thumb16_STR_reg(Reg m, Reg n, Reg t) {
        return fmt::format("str {}, [{}, {}]", t, n, m);
    }

    std::string thumb16_STRH_reg(Reg m, Reg n, Reg t) {
        return fmt::format("strh {}, [{}, {}]", t, n, m);
    }

    std::string thumb16_STRB_reg(Reg m, Reg n, Reg t) {
        return fmt::format("strb {}, [{}, {}]", t, n, m);
    }

    std::string thumb16_LDRSB_reg(Reg m, Reg n, Reg t) {
        return fmt::format("ldrsb {}, [{}, {}]", t, n, m);
    }

    std::string thumb16_LDR_reg(Reg m, Reg n, Reg t) {
        return fmt::format("ldr {}, [{}, {}]", t, n, m);
    }

    std::string thumb16_LDRH_reg(Reg m, Reg n, Reg t) {
        return fmt::format("ldrh {}, [%s, %s]", t, n, m);
    }

    std::string thumb16_LDRB_reg(Reg m, Reg n, Reg t) {
        return fmt::format("ldrb {}, [{}, {}]", t, n, m);
    }

    std::string thumb16_LDRSH_reg(Reg m, Reg n, Reg t) {
        return fmt::format("ldrsh {}, [{}, {}]", t, n, m);
    }

    std::string thumb16_STR_imm_t1(Imm5 imm5, Reg n, Reg t) {
        u32 imm32 = imm5 << 2;
        return fmt::format("str {}, [{}, #{}]", t, n, imm32);
    }

    std::string thumb16_LDR_imm_t1(Imm5 imm5, Reg n, Reg t) {
        u32 imm32 = imm5 << 2;
        return fmt::format("ldr {}, [{}, #{}]", t, n, imm32);
    }

    std::string thumb16_STRB_imm(Imm5 imm5, Reg n, Reg t) {
        u32 imm32 = imm5;
        return fmt::format("strb {}, [{}, #{}]", t, n, imm32);
    }

    std::string thumb16_LDRB_imm(Imm5 imm5, Reg n, Reg t) {
        u32 imm32 = imm5;
        return fmt::format("ldrb {}, [{}, #{}]", t, n, imm32);
    }

    std::string thumb16_STRH_imm(Imm5 imm5, Reg n, Reg t) {
        u32 imm32 = imm5 << 1;
        return fmt::format("strh {}, [{}, #{}]", t, n, imm32);
    }

    std::string thumb16_LDRH_imm(Imm5 imm5, Reg n, Reg t) {
        u32 imm32 = imm5 << 1;
        return fmt::format("ldrh {}, [{}, #{}]", t, n, imm32);
    }

    std::string thumb16_STR_imm_t2(Reg t, Imm5 imm5) {
        u32 imm32 = imm5 << 2;
        return fmt::format("str {}, [sp, #{}]", t, imm32);
    }

    std::string thumb16_LDR_imm_t2(Reg t, Imm5 imm5) {
        u32 imm32 = imm5 << 2;
        return fmt::format("ldr {}, [sp, #{}]", t, imm32);
    }

    std::string thumb16_ADR(Reg d, Imm8 imm8) {
        u32 imm32 = imm8 << 2;
        return fmt::format("adr {}, +#{}", d, imm32);
    }

    std::string thumb16_ADD_sp_t1(Reg d, Imm8 imm8) {
        u32 imm32 = imm8 << 2;
        return fmt::format("add {}, sp, #{}", d, imm32);
    }

    std::string thumb16_ADD_sp_t2(Imm7 imm7) {
        u32 imm32 = imm7 << 2;
        return fmt::format("add sp, sp, #{}", imm32);
    }

    std::string thumb16_SUB_sp(Imm7 imm7) {
        u32 imm32 = imm7 << 2;
        return fmt::format("sub sp, sp, #{}", imm32);
    }

    std::string thumb16_SXTH(Reg m, Reg d) {
        return fmt::format("sxth {}, {}", d, m);
    }

    std::string thumb16_SXTB(Reg m, Reg d) {
        return fmt::format("sxtb {}, {}", d, m);
    }

    std::string thumb16_UXTH(Reg m, Reg d) {
        return fmt::format("uxth {}, {}", d, m);
    }

    std::string thumb16_UXTB(Reg m, Reg d) {
        return fmt::format("uxtb {}, {}", d, m);
    }

    std::string thumb16_PUSH(bool M, RegList reg_list) {
        if (M) reg_list |= 1 << 14;
        return fmt::format("push {{{}}}", RegListToString(reg_list));
    }

    std::string thumb16_POP(bool P, RegList reg_list) {
        if (P) reg_list |= 1 << 15;
        return fmt::format("pop {{{}}}", RegListToString(reg_list));
    }

    std::string thumb16_SETEND(bool E) {
        return fmt::format("setend {}", E ? "BE" : "LE");
    }

    std::string thumb16_CPS(bool im, bool a, bool i, bool f) {
        return fmt::format("cps{} {}{}{}", im ? "id" : "ie", a ? "a" : "", i ? "i" : "", f ? "f" : "");
    }

    std::string thumb16_REV(Reg m, Reg d) {
        return fmt::format("rev {}, {}", d, m);
    }

    std::string thumb16_REV16(Reg m, Reg d) {
        return fmt::format("rev16 {}, {}", d, m);
    }

    std::string thumb16_REVSH(Reg m, Reg d) {
        return fmt::format("revsh {}, {}", d, m);
    }

    std::string thumb16_STMIA(Reg n, RegList reg_list) {
        return fmt::format("stm {}!, {{{}}}", n, RegListToString(reg_list));
    }

    std::string thumb16_LDMIA(Reg n, RegList reg_list) {
        bool write_back = !Common::Bit(static_cast<size_t>(n), reg_list);
        return fmt::format("ldm {}{}, {{{}}}", n, write_back ? "!" : "", RegListToString(reg_list));
    }

    std::string thumb16_BX(Reg m) {
        return fmt::format("bx {}", m);
    }

    std::string thumb16_BLX_reg(Reg m) {
        return fmt::format("blx {}", m);
    }

    std::string thumb16_UDF() {
        return fmt::format("udf");
    }

    std::string thumb16_SVC(Imm8 imm8) {
        return fmt::format("svc #{}", imm8);
    }

    std::string thumb16_B_t1(Cond cond, Imm8 imm8) {
        s32 imm32 = Common::SignExtend<9, s32>(imm8 << 1) + 4;
        return fmt::format("b{} {}#{}", CondToString(cond), Common::SignToChar(imm32), abs(imm32));
    }

    std::string thumb16_B_t2(Imm11 imm11) {
        s32 imm32 = Common::SignExtend<12, s32>(imm11 << 1) + 4;
        return fmt::format("b {}#{}", Common::SignToChar(imm32), abs(imm32));
    }
};

std::string DisassembleThumb16(u16 instruction) {
    DisassemblerVisitor visitor;
    auto decoder = DecodeThumb16<DisassemblerVisitor>(instruction);
    return !decoder ? fmt::format("UNKNOWN: {:x}", instruction) : decoder->get().call(visitor, instruction);
}

} // namespace Dynarmic::A32
