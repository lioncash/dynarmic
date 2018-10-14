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
#include "frontend/A32/decoder/arm.h"
#include "frontend/A32/decoder/vfp2.h"
#include "frontend/A32/disassembler/disassembler.h"
#include "frontend/A32/types.h"

namespace Dynarmic::A32 {

class DisassemblerVisitor {
public:
    using instruction_return_type = std::string;

    u32 ArmExpandImm(int rotate, Imm8 imm8) {
        return Common::RotateRight(static_cast<u32>(imm8), rotate*2);
    }

    std::string ShiftStr(ShiftType shift, Imm5 imm5) {
        switch (shift) {
        case ShiftType::LSL:
            if (imm5 == 0) return "";
            return fmt::format(", lsl #{}", imm5);
        case ShiftType::LSR:
            if (imm5 == 0) return ", lsr #32";
            return fmt::format(", lsr #{}", imm5);
        case ShiftType::ASR:
            if (imm5 == 0) return ", asr #32";
            return fmt::format(", asr #{}", imm5);
        case ShiftType::ROR:
            if (imm5 == 0) return ", rrx";
            return fmt::format(", ror #{}", imm5);
        }
        ASSERT(false);
        return "<internal error>";
    }

    std::string RsrStr(Reg s, ShiftType shift, Reg m) {
        switch (shift){
        case ShiftType::LSL:
            return fmt::format("{}, lsl {}", m, s);
        case ShiftType::LSR:
            return fmt::format("{}, lsr {}", m, s);
        case ShiftType::ASR:
            return fmt::format("{}, asr {}", m, s);
        case ShiftType::ROR:
            return fmt::format("{}, ror {}", m, s);
        }
        ASSERT(false);
        return "<internal error>";
    }

    std::string RorStr(Reg m, SignExtendRotation rotate) {
        switch (rotate) {
        case SignExtendRotation::ROR_0:
            return RegToString(m);
        case SignExtendRotation::ROR_8:
            return fmt::format("{}, ror #8", m);
        case SignExtendRotation::ROR_16:
            return fmt::format("{}, ror #16", m);
        case SignExtendRotation::ROR_24:
            return fmt::format("{}, ror #24", m);
        }
        ASSERT(false);
        return "<internal error>";
    }

    std::string FPRegStr(bool dp_operation, size_t base, bool bit) {
        size_t reg_num;
        if (dp_operation) {
            reg_num = base + (bit ? 16 : 0);
        } else {
            reg_num = (base << 1) + (bit ? 1 : 0);
        }
        return fmt::format("{}{}", dp_operation ? 'd' : 's', reg_num);
    }

    std::string FPNextRegStr(bool dp_operation, size_t base, bool bit) {
        size_t reg_num;
        if (dp_operation) {
            reg_num = base + (bit ? 16 : 0);
        } else {
            reg_num = (base << 1) + (bit ? 1 : 0);
        }
        return fmt::format("{}{}", dp_operation ? 'd' : 's', reg_num + 1);
    }

    std::string CondOrTwo(Cond cond) {
        return cond == Cond::NV ? "2" : CondToString(cond);
    }

    // Branch instructions
    std::string arm_B(Cond cond, Imm24 imm24) {
        s32 offset = Common::SignExtend<26, s32>(imm24 << 2) + 8;
        return fmt::format("b{} {}#{}", CondToString(cond), Common::SignToChar(offset), abs(offset));
    }
    std::string arm_BL(Cond cond, Imm24 imm24) {
        s32 offset = Common::SignExtend<26, s32>(imm24 << 2) + 8;
        return fmt::format("bl{} {}#{}", CondToString(cond), Common::SignToChar(offset), abs(offset));
    }
    std::string arm_BLX_imm(bool H, Imm24 imm24) {
        s32 offset = Common::SignExtend<26, s32>(imm24 << 2) + 8 + (H ? 2 : 0);
        return fmt::format("blx {}#{}", Common::SignToChar(offset), abs(offset));
    }
    std::string arm_BLX_reg(Cond cond, Reg m) {
        return fmt::format("blx{} {}", CondToString(cond), m);
    }
    std::string arm_BX(Cond cond, Reg m) {
        return fmt::format("bx{} {}", CondToString(cond), m);
    }
    std::string arm_BXJ(Cond cond, Reg m) {
        return fmt::format("bxj{} {}", CondToString(cond), m);
    }

    // Coprocessor instructions
    std::string arm_CDP(Cond cond, size_t opc1, CoprocReg CRn, CoprocReg CRd, size_t coproc_no, size_t opc2, CoprocReg CRm) {
        return fmt::format("cdp{} p{}, #{}, {}, {}, {}, #{}", CondToString(cond), coproc_no, opc1, CRd, CRn, CRm, opc2);
    }

    std::string arm_LDC(Cond cond, bool p, bool u, bool d, bool w, Reg n, CoprocReg CRd, size_t coproc_no, Imm8 imm8) {
        const u32 imm32 = static_cast<u32>(imm8) << 2;
        if (!p && !u && !d && !w)
            return "<undefined>";
        if (p)
            return fmt::format("ldc{}{} {}, {}, [{}, #{}{}]{}", d ? "l" : "", CondOrTwo(cond), coproc_no, CRd, n, u ? "+" : "-", imm32, w ? "!" : "");
        if (!p && w)
            return fmt::format("ldc{}{} {}, {}, [{}], #{}{}", d ? "l" : "", CondOrTwo(cond), coproc_no, CRd, n, u ? "+" : "-", imm32);
        if (!p && !w && u)
            return fmt::format("ldc{}{} {}, {}, [{}], {}", d ? "l" : "", CondOrTwo(cond), coproc_no, CRd, n, imm8);
        UNREACHABLE();
        return "<internal error>";
    }

    std::string arm_MCR(Cond cond, size_t opc1, CoprocReg CRn, Reg t, size_t coproc_no, size_t opc2, CoprocReg CRm) {
        return fmt::format("mcr{} p{}, #{}, {}, {}, {}, #{}", CondOrTwo(cond), coproc_no, opc1, t, CRn, CRm, opc2);
    }

    std::string arm_MCRR(Cond cond, Reg t2, Reg t, size_t coproc_no, size_t opc, CoprocReg CRm) {
        return fmt::format("mcr{} p{}, #{}, {}, {}, {}", CondOrTwo(cond), coproc_no, opc, t, t2, CRm);
    }

    std::string arm_MRC(Cond cond, size_t opc1, CoprocReg CRn, Reg t, size_t coproc_no, size_t opc2, CoprocReg CRm) {
        return fmt::format("mrc{} p{}, #{}, {}, {}, {}, #{}", CondOrTwo(cond), coproc_no, opc1, t, CRn, CRm, opc2);
    }

    std::string arm_MRRC(Cond cond, Reg t2, Reg t, size_t coproc_no, size_t opc, CoprocReg CRm) {
        return fmt::format("mrrc{} p{}, #{}, {}, {}, {}", CondOrTwo(cond), coproc_no, opc, t, t2, CRm);
    }

    std::string arm_STC(Cond cond, bool p, bool u, bool d, bool w, Reg n, CoprocReg CRd, size_t coproc_no, Imm8 imm8) {
        const u32 imm32 = static_cast<u32>(imm8) << 2;
        if (!p && !u && !d && !w)
            return "<undefined>";
        if (p)
            return fmt::format("stc{}{} {}, {}, [{}, #{}{}]{}", d ? "l" : "", CondOrTwo(cond), coproc_no, CRd, n, u ? "+" : "-", imm32, w ? "!" : "");
        if (!p && w)
            return fmt::format("stc{}{} {}, {}, [{}], #{}{}", d ? "l" : "", CondOrTwo(cond), coproc_no, CRd, n, u ? "+" : "-", imm32);
        if (!p && !w && u)
            return fmt::format("stc{}{} {}, {}, [{}], {}", d ? "l" : "", CondOrTwo(cond), coproc_no, CRd, n, imm8);
        UNREACHABLE();
        return "<internal error>";
    }

    // Data processing instructions
    std::string arm_ADC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("adc{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_ADC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("adc{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_ADC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("adc{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_ADD_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("add{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_ADD_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("add{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_ADD_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("add{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_AND_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("and{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_AND_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("and{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_AND_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("and{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_BIC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("bic{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_BIC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("bic{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_BIC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("bic{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_CMN_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
        return fmt::format("cmn{} {}, #{}", CondToString(cond), n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_CMN_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("cmn{} {}, {}{}", CondToString(cond), n, m, ShiftStr(shift, imm5));
    }
    std::string arm_CMN_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
        return fmt::format("cmn{} {}, {}", CondToString(cond), n, RsrStr(s, shift, m));
    }
    std::string arm_CMP_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
        return fmt::format("cmp{} {}, #{}", CondToString(cond), n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_CMP_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("cmp{} {}, {}{}", CondToString(cond), n, m, ShiftStr(shift, imm5));
    }
    std::string arm_CMP_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
        return fmt::format("cmp{} {}, {}", CondToString(cond), n, RsrStr(s, shift, m));
    }
    std::string arm_EOR_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("eor{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_EOR_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("eor{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_EOR_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("eor{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_MOV_imm(Cond cond, bool S, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("mov{}{} {}, #{}", CondToString(cond), S ? "s" : "", d, ArmExpandImm(rotate, imm8));
    }
    std::string arm_MOV_reg(Cond cond, bool S, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("mov{}{} {}, {}{}", CondToString(cond), S ? "s" : "", d, m, ShiftStr(shift, imm5));
    }
    std::string arm_MOV_rsr(Cond cond, bool S, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("mov{}{} {}, {}", CondToString(cond), S ? "s" : "", d, RsrStr(s, shift, m));
    }
    std::string arm_MVN_imm(Cond cond, bool S, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("mvn{}{} {}, #{}", CondToString(cond), S ? "s" : "", d, ArmExpandImm(rotate, imm8));
    }
    std::string arm_MVN_reg(Cond cond, bool S, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("mvn{}{} {}, {}{}", CondToString(cond), S ? "s" : "", d, m, ShiftStr(shift, imm5));
    }
    std::string arm_MVN_rsr(Cond cond, bool S, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("mvn{}{} {}, {}", CondToString(cond), S ? "s" : "", d, RsrStr(s, shift, m));
    }
    std::string arm_ORR_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("orr{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_ORR_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("orr{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_ORR_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("orr{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_RSB_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("rsb{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_RSB_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("rsb{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_RSB_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("rsb{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_RSC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("rsc{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_RSC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("rsc{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_RSC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("rsc{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_SBC_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("sbc{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_SBC_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("sbc{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_SBC_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("sbc{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_SUB_imm(Cond cond, bool S, Reg n, Reg d, int rotate, Imm8 imm8) {
        return fmt::format("sub{}{} {}, {}, #{}", CondToString(cond), S ? "s" : "", d, n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_SUB_reg(Cond cond, bool S, Reg n, Reg d, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("sub{}{} {}, {}, {}{}", CondToString(cond), S ? "s" : "", d, n, m, ShiftStr(shift, imm5));
    }
    std::string arm_SUB_rsr(Cond cond, bool S, Reg n, Reg d, Reg s, ShiftType shift, Reg m) {
        return fmt::format("sub{}{} {}, {}, {}", CondToString(cond), S ? "s" : "", d, n, RsrStr(s, shift, m));
    }
    std::string arm_TEQ_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
        return fmt::format("teq{} {}, #{}", CondToString(cond), n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_TEQ_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("teq{} {}, {}{}", CondToString(cond), n, m, ShiftStr(shift, imm5));
    }
    std::string arm_TEQ_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
        return fmt::format("teq{} {}, {}", CondToString(cond), n, RsrStr(s, shift, m));
    }
    std::string arm_TST_imm(Cond cond, Reg n, int rotate, Imm8 imm8) {
        return fmt::format("tst{} {}, #{}", CondToString(cond), n, ArmExpandImm(rotate, imm8));
    }
    std::string arm_TST_reg(Cond cond, Reg n, Imm5 imm5, ShiftType shift, Reg m) {
        return fmt::format("tst{} {}, {}{}", CondToString(cond), n, m, ShiftStr(shift, imm5));
    }
    std::string arm_TST_rsr(Cond cond, Reg n, Reg s, ShiftType shift, Reg m) {
        return fmt::format("tst{} {}, {}", CondToString(cond), n, RsrStr(s, shift, m));
    }

    // Exception generation instructions
    std::string arm_BKPT(Cond cond, Imm12 imm12, Imm4 imm4) {
        return fmt::format("bkpt{} #{}", CondToString(cond), imm12 << 4 | imm4);
    }
    std::string arm_SVC(Cond cond, Imm24 imm24) {
        return fmt::format("svc{} #{}", CondToString(cond), imm24);
    }
    std::string arm_UDF() {
        return fmt::format("udf");
    }

    // Extension functions
    std::string arm_SXTAB(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("sxtab{} {}, {}, {}", CondToString(cond), d, n, RorStr(m, rotate));
    }
    std::string arm_SXTAB16(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("sxtab16{} {}, {}, {}", CondToString(cond), d, n, RorStr(m, rotate));
    }
    std::string arm_SXTAH(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("sxtah{} {}, {}, {}", CondToString(cond), d, n, RorStr(m, rotate));
    }
    std::string arm_SXTB(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("sxtb{} {}, {}", CondToString(cond), d, RorStr(m, rotate));
    }
    std::string arm_SXTB16(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("sxtb16{} {}, {}", CondToString(cond), d, RorStr(m, rotate));
    }
    std::string arm_SXTH(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("sxth{} {}, {}", CondToString(cond), d, RorStr(m, rotate));
    }
    std::string arm_UXTAB(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("uxtab{} {}, {}, {}", CondToString(cond), d, n, RorStr(m, rotate));
    }
    std::string arm_UXTAB16(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("uxtab16{} {}, {}, {}", CondToString(cond), d, n, RorStr(m, rotate));
    }
    std::string arm_UXTAH(Cond cond, Reg n, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("uxtah{} {}, {}, {}", CondToString(cond), d, n, RorStr(m, rotate));
    }
    std::string arm_UXTB(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("uxtb{} {}, {}", CondToString(cond), d, RorStr(m, rotate));
    }
    std::string arm_UXTB16(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("uxtb16{} {}, {}", CondToString(cond), d, RorStr(m, rotate));
    }
    std::string arm_UXTH(Cond cond, Reg d, SignExtendRotation rotate, Reg m) {
        return fmt::format("uxth{} {}, {}", CondToString(cond), d, RorStr(m, rotate));
    }

    // Hint instructions
    std::string arm_PLD() { return "pld <unimplemented>"; }
    std::string arm_SEV() { return "sev <unimplemented>"; }
    std::string arm_WFE() { return "wfe <unimplemented>"; }
    std::string arm_WFI() { return "wfi <unimplemented>"; }
    std::string arm_YIELD() { return "yield <unimplemented>"; }

    // Load/Store instructions
    std::string arm_LDR_lit(Cond cond, bool U, Reg t, Imm12 imm12) {
        bool P = true, W = false;
        return arm_LDR_imm(cond, P, U, W, Reg::PC, t, imm12);
    }
    std::string arm_LDR_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm12 imm12) {
        if (P) {
            return fmt::format("ldr{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? "!" : "");
        } else {
            return fmt::format("ldr{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDR_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm5 imm5, ShiftType shift, Reg m) {
        if (P) {
            return fmt::format("ldr{} {}, [{}, {}{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? "!" : "");
        } else {
            return fmt::format("ldr{} {}, [{}], {}{}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRB_lit(Cond cond, bool U, Reg t, Imm12 imm12) {
        bool P = true, W = false;
        return arm_LDRB_imm(cond, P, U, W, Reg::PC, t, imm12);
    }
    std::string arm_LDRB_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm12 imm12) {
        if (P) {
            return fmt::format("ldrb{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? "!" : "");
        } else {
            return fmt::format("ldrb{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRB_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm5 imm5, ShiftType shift, Reg m) {
        if (P) {
            return fmt::format("ldrb{} {}, [{}, {}{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? "!" : "");
        } else {
            return fmt::format("ldrb{} {}, [{}], {}{}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRBT() { return "ice"; }
    std::string arm_LDRD_lit(Cond cond, bool U, Reg t, Imm4 imm8a, Imm4 imm8b) {
        bool P = true, W = false;
        return arm_LDRD_imm(cond, P, U, W, Reg::PC, t, imm8a, imm8b);
    }
    std::string arm_LDRD_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm4 imm8a, Imm4 imm8b) {
        u32 imm32 = (imm8a << 4) | imm8b;
        if (P) {
            return fmt::format("ldrd{} {}, {}, [{}, #{}{}]{}", CondToString(cond), t, t+1, n, U ? '+' : '-', imm32, W ? "!" : "");
        } else {
            return fmt::format("ldrd{} {}, {}, [{}], #{}{}{}", CondToString(cond), t, t+1, n, U ? '+' : '-', imm32, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRD_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Reg m) {
        if (P) {
            return fmt::format("ldrd{} {}, {}, [{}, {}{}]{}", CondToString(cond), t, t+1, n, U ? '+' : '-', m, W ? "!" : "");
        } else {
            return fmt::format("ldrd{} {}, {}, [{}], {}{}{}", CondToString(cond), t, t+1, n, U ? '+' : '-', m, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRH_lit(Cond cond, bool P, bool U, bool W, Reg t, Imm4 imm8a, Imm4 imm8b) {
        return arm_LDRH_imm(cond, P, U, W, Reg::PC, t, imm8a, imm8b);
    }
    std::string arm_LDRH_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm4 imm8a, Imm4 imm8b) {
        u32 imm32 = (imm8a << 4) | imm8b;
        if (P) {
            return fmt::format("ldrh{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? "!" : "");
        } else {
            return fmt::format("ldrh{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRH_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Reg m) {
        if (P) {
            return fmt::format("ldrh{} {}, [{}, {}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? "!" : "");
        } else {
            return fmt::format("ldrh{} {}, [{}], {}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRHT() { return "ice"; }
    std::string arm_LDRSB_lit(Cond cond, bool U, Reg t, Imm4 imm8a, Imm4 imm8b) {
        bool P = true, W = false;
        return arm_LDRSB_imm(cond, P, U, W, Reg::PC, t, imm8a, imm8b);
    }
    std::string arm_LDRSB_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm4 imm8a, Imm4 imm8b) {
        u32 imm32 = (imm8a << 4) | imm8b;
        if (P) {
            return fmt::format("ldrsb{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? "!" : "");
        } else {
            return fmt::format("ldrsb{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRSB_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Reg m) {
        if (P) {
            return fmt::format("ldrsb{} {}, [{}, {}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? "!" : "");
        } else {
            return fmt::format("ldrsb{} {}, [{}], {}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRSBT() { return "ice"; }
    std::string arm_LDRSH_lit(Cond cond, bool U, Reg t, Imm4 imm8a, Imm4 imm8b) {
        bool P = true, W = false;
        return arm_LDRSH_imm(cond, P, U, W, Reg::PC, t, imm8a, imm8b);
    }
    std::string arm_LDRSH_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm4 imm8a, Imm4 imm8b) {
        u32 imm32 = (imm8a << 4) | imm8b;
        if (P) {
            return fmt::format("ldrsh{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? "!" : "");
        } else {
            return fmt::format("ldrsh{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRSH_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Reg m) {
        if (P) {
            return fmt::format("ldrsh{} {}, [{}, {}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? "!" : "");
        } else {
            return fmt::format("ldrsh{} {}, [{}], {}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_LDRSHT() { return "ice"; }
    std::string arm_LDRT() { return "ice"; }
    std::string arm_STR_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm12 imm12) {
        if (P) {
            return fmt::format("str{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? "!" : "");
        } else {
            return fmt::format("str{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STR_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm5 imm5, ShiftType shift, Reg m) {
        if (P) {
            return fmt::format("str{} {}, [{}, {}{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? "!" : "");
        } else {
            return fmt::format("str{} {}, [{}], {}{}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STRB_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm12 imm12) {
        if (P) {
            return fmt::format("strb{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? "!" : "");
        } else {
            return fmt::format("strb{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm12, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STRB_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm5 imm5, ShiftType shift, Reg m) {
        if (P) {
            return fmt::format("strb{} {}, [{}, {}{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? "!" : "");
        } else {
            return fmt::format("strb{} {}, [{}], {}{}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, ShiftStr(shift, imm5), W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STRBT() { return "ice"; }
    std::string arm_STRD_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm4 imm8a, Imm4 imm8b) {
        u32 imm32 = (imm8a << 4) | imm8b;
        if (P) {
            return fmt::format("strd{} {}, {}, [{}, #{}{}]{}", CondToString(cond), t, t+1, n, U ? '+' : '-', imm32, W ? "!" : "");
        } else {
            return fmt::format("strd{} {}, {}, [{}], #{}{}{}", CondToString(cond), t, t+1, n, U ? '+' : '-', imm32, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STRD_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Reg m) {
        if (P) {
            return fmt::format("strd{} {}, {}, [{}, {}{}]{}", CondToString(cond), t, t+1, n, U ? '+' : '-', m, W ? "!" : "");
        } else {
            return fmt::format("strd{} {}, {}, [{}], {}{}{}", CondToString(cond), t, t+1, n, U ? '+' : '-', m, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STRH_imm(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Imm4 imm8a, Imm4 imm8b) {
        u32 imm32 = (imm8a << 4) | imm8b;
        if (P) {
            return fmt::format("strh{} {}, [{}, #{}{}]{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? "!" : "");
        } else {
            return fmt::format("strh{} {}, [{}], #{}{}{}", CondToString(cond), t, n, U ? '+' : '-', imm32, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STRH_reg(Cond cond, bool P, bool U, bool W, Reg n, Reg t, Reg m) {
        if (P) {
            return fmt::format("strd{} {}, [{}, {}{}]{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? "!" : "");
        } else {
            return fmt::format("strd{} {}, [{}], {}{}{}", CondToString(cond), t, n, U ? '+' : '-', m, W ? " (err: W == 1!!!)" : "");
        }
    }
    std::string arm_STRHT() { return "ice"; }
    std::string arm_STRT() { return "ice"; }

    // Load/Store multiple instructions
    std::string arm_LDM(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("ldm{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_LDMDA(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("ldmda{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_LDMDB(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("ldmdb{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_LDMIB(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("ldmib{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_LDM_usr() { return "ice"; }
    std::string arm_LDM_eret() { return "ice"; }
    std::string arm_STM(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("stm{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_STMDA(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("stmda{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_STMDB(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("stmdb{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_STMIB(Cond cond, bool W, Reg n, RegList list) {
        return fmt::format("stmib{} {}{}, {{{}}}", CondToString(cond), n, W ? "!" : "", RegListToString(list));
    }
    std::string arm_STM_usr() { return "ice"; }

    // Miscellaneous instructions
    std::string arm_CLZ(Cond cond, Reg d, Reg m) {
        return fmt::format("clz{} {}, {}", CondToString(cond), d, m);
    }
    std::string arm_NOP() {
        return "nop";
    }
    std::string arm_SEL(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("sel{} {}, {}, {}", CondToString(cond), d, n, m);
    }

    // Unsigned sum of absolute difference functions
    std::string arm_USAD8(Cond cond, Reg d, Reg m, Reg n) {
        return fmt::format("usad8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_USADA8(Cond cond, Reg d, Reg a, Reg m, Reg n) {
        return fmt::format("usad8a{} {}, {}, {}, {}", CondToString(cond), d, n, m, a);
    }

    // Packing instructions
    std::string arm_PKHBT(Cond cond, Reg n, Reg d, Imm5 imm5, Reg m) {
        return fmt::format("pkhbt{} {}, {}, {}{}", CondToString(cond), d, n, m, ShiftStr(ShiftType::LSL, imm5));
    }
    std::string arm_PKHTB(Cond cond, Reg n, Reg d, Imm5 imm5, Reg m) {
        return fmt::format("pkhtb{} {}, {}, {}{}", CondToString(cond), d, n, m, ShiftStr(ShiftType::ASR, imm5));
    }

    // Reversal instructions
    std::string arm_REV(Cond cond, Reg d, Reg m) {
        return fmt::format("rev{} {}, {}", CondToString(cond), d, m);
    }
    std::string arm_REV16(Cond cond, Reg d, Reg m) {
        return fmt::format("rev16{} {}, {}", CondToString(cond), d, m);
    }
    std::string arm_REVSH(Cond cond, Reg d, Reg m) {
        return fmt::format("revsh{} {}, {}", CondToString(cond), d, m);
    }

    // Saturation instructions
    std::string arm_SSAT(Cond cond, Imm5 sat_imm, Reg d, Imm5 imm5, bool sh, Reg n) {
        return fmt::format("ssat{} {}, #{}, {}{}", CondToString(cond), d, sat_imm + 1, n, ShiftStr(ShiftType(sh << 1), imm5));
    }
    std::string arm_SSAT16(Cond cond, Imm4 sat_imm, Reg d, Reg n) {
        return fmt::format("ssat16{} {}, #{}, {}", CondToString(cond), d, sat_imm + 1, n);
    }
    std::string arm_USAT(Cond cond, Imm5 sat_imm, Reg d, Imm5 imm5, bool sh, Reg n) {
        return fmt::format("usat{} {}, #{}, {}{}", CondToString(cond), d, sat_imm, n, ShiftStr(ShiftType(sh << 1), imm5));
    }
    std::string arm_USAT16(Cond cond, Imm4 sat_imm, Reg d, Reg n) {
        return fmt::format("usat16{} {}, #{}, {}", CondToString(cond), d, sat_imm, n);
    }

    // Multiply (Normal) instructions
    std::string arm_MLA(Cond cond, bool S, Reg d, Reg a, Reg m, Reg n) {
        return fmt::format("mla{}{} {}, {}, {}, {}", S ? "s" : "", CondToString(cond), d, n, m, a);
    }
    std::string arm_MUL(Cond cond, bool S, Reg d, Reg m, Reg n) {
        return fmt::format("mul{}{} {}, {}, {}", S ? "s" : "", CondToString(cond), d, n, m);
    }

    // Multiply (Long) instructions
    std::string arm_SMLAL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
        return fmt::format("smlal{}{} {}, {}, {}, {}", S ? "s" : "", CondToString(cond), dLo, dHi, n, m);
    }
    std::string arm_SMULL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
        return fmt::format("smull{}{} {}, {}, {}, {}", S ? "s" : "", CondToString(cond), dLo, dHi, n, m);
    }
    std::string arm_UMAAL(Cond cond, Reg dHi, Reg dLo, Reg m, Reg n) {
        return fmt::format("umaal{} {}, {}, {}, {}", CondToString(cond), dLo, dHi, n, m);
    }
    std::string arm_UMLAL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
        return fmt::format("umlal{}{} {}, {}, {}, {}", S ? "s" : "", CondToString(cond), dLo, dHi, n, m);
    }
    std::string arm_UMULL(Cond cond, bool S, Reg dHi, Reg dLo, Reg m, Reg n) {
        return fmt::format("umull{}{} {}, {}, {}, {}", S ? "s" : "", CondToString(cond), dLo, dHi, n, m);
    }

    // Multiply (Halfword) instructions
    std::string arm_SMLALxy(Cond cond, Reg dHi, Reg dLo, Reg m, bool M, bool N, Reg n) {
        return fmt::format("smlal{}{}{} {}, {}, {}, {}", N ? 't' : 'b', M ? 't' : 'b', CondToString(cond), dLo, dHi, n, m);
    }
    std::string arm_SMLAxy(Cond cond, Reg d, Reg a, Reg m, bool M, bool N, Reg n) {
        return fmt::format("smla{}{}{} {}, {}, {}, {}", N ? 't' : 'b', M ? 't' : 'b', CondToString(cond), d, n, m, a);
    }
    std::string arm_SMULxy(Cond cond, Reg d, Reg m, bool M, bool N, Reg n) {
        return fmt::format("smul{}{}{} {}, {}, {}", N ? 't' : 'b', M ? 't' : 'b', CondToString(cond), d, n, m);
    }

    // Multiply (word by halfword) instructions
    std::string arm_SMLAWy(Cond cond, Reg d, Reg a, Reg m, bool M, Reg n) {
        return fmt::format("smlaw{}{} {}, {}, {}, {}", M ? 't' : 'b', CondToString(cond), d, n, m, a);
    }
    std::string arm_SMULWy(Cond cond, Reg d, Reg m, bool M, Reg n) {
        return fmt::format("smulw{}{} {}, {}, {}", M ? 't' : 'b', CondToString(cond), d, n, m);
    }

    // Multiply (Most significant word) instructions
    std::string arm_SMMLA(Cond cond, Reg d, Reg a, Reg m, bool R, Reg n) {
        return fmt::format("smmla{}{} {}, {}, {}, {}", R ? "r" : "", CondToString(cond), d, n, m, a);
    }
    std::string arm_SMMLS(Cond cond, Reg d, Reg a, Reg m, bool R, Reg n) {
        return fmt::format("smmls{}{} {}, {}, {}, {}", R ? "r" : "", CondToString(cond), d, n, m, a);
    }
    std::string arm_SMMUL(Cond cond, Reg d, Reg m, bool R, Reg n) {
        return fmt::format("smmul{}{} {}, {}, {}", R ? "r" : "", CondToString(cond), d, n, m);
    }

    // Multiply (Dual) instructions
    std::string arm_SMLAD(Cond cond, Reg d, Reg a, Reg m, bool M, Reg n) {
        return fmt::format("smlad{}{} {}, {}, {}, {}", M ? "x" : "", CondToString(cond), d, n, m, a);
    }
    std::string arm_SMLALD(Cond cond, Reg dHi, Reg dLo, Reg m, bool M, Reg n) {
        return fmt::format("smlald{}{} {}, {}, {}, {}", M ? "x" : "", CondToString(cond), dLo, dHi, n, m);
    }
    std::string arm_SMLSD(Cond cond, Reg d, Reg a, Reg m, bool M, Reg n) {
        return fmt::format("smlsd{}{} {}, {}, {}, {}", M ? "x" : "", CondToString(cond), d, n, m, a);
    }
    std::string arm_SMLSLD(Cond cond, Reg dHi, Reg dLo, Reg m, bool M, Reg n) {
        return fmt::format("smlsld{}{} {}, {}, {}, {}", M ? "x" : "", CondToString(cond), dLo, dHi, n, m);
    }
    std::string arm_SMUAD(Cond cond, Reg d, Reg m, bool M, Reg n) {
        return fmt::format("smuad{}{} {}, {}, {}", M ? "x" : "", CondToString(cond), d, n, m);
    }
    std::string arm_SMUSD(Cond cond, Reg d, Reg m, bool M, Reg n) {
        return fmt::format("smusd{}{} {}, {}, {}", M ? "x" : "", CondToString(cond), d, n, m);
    }

    // Parallel Add/Subtract (Modulo arithmetic) instructions
    std::string arm_SADD8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("sadd8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SADD16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("sadd16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SASX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("sasx{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SSAX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("ssax{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SSUB8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("ssub8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SSUB16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("ssub16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UADD8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uadd8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UADD16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uadd16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UASX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uasx{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_USAX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("usax{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_USUB8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("usub8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_USUB16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("usub16{} {}, {}, {}", CondToString(cond), d, n, m);
    }

    // Parallel Add/Subtract (Saturating) instructions
    std::string arm_QADD8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qadd8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_QADD16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qadd16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_QASX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qasx{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_QSAX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qsax{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_QSUB8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qsub8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_QSUB16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qsub16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UQADD8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uqadd8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UQADD16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uqadd16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UQASX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uqasx{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UQSAX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uqsax{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UQSUB8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uqsub8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UQSUB16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uqsub16{} {}, {}, {}", CondToString(cond), d, n, m);
    }

    // Parallel Add/Subtract (Halving) instructions
    std::string arm_SHADD8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("shadd8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SHADD16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("shadd16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SHASX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("shasx{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SHSAX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("shsax{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SHSUB8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("shsub8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_SHSUB16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("shsub16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UHADD8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uhadd8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UHADD16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uhadd16{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UHASX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uhasx{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UHSAX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uhsax{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UHSUB8(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uhsub8{} {}, {}, {}", CondToString(cond), d, n, m);
    }
    std::string arm_UHSUB16(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("uhsub16{} {}, {}, {}", CondToString(cond), d, n, m);
    }

    // Saturated Add/Subtract instructions
    std::string arm_QADD(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qadd{} {}, {}, {}", CondToString(cond), d, m, n);
    }
    std::string arm_QSUB(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qsub{} {}, {}, {}", CondToString(cond), d, m, n);
    }
    std::string arm_QDADD(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qdadd{} {}, {}, {}", CondToString(cond), d, m, n);
    }
    std::string arm_QDSUB(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("qdsub{} {}, {}, {}", CondToString(cond), d, m, n);
    }

    // Synchronization Primitive instructions
    std::string arm_CLREX() {
        return "clrex";
    }
    std::string arm_LDREX(Cond cond, Reg n, Reg d) {
        return fmt::format("ldrex{} {}, [{}]", CondToString(cond), d, n);
    }
    std::string arm_LDREXB(Cond cond, Reg n, Reg d) {
        return fmt::format("ldrexb{} {}, [{}]", CondToString(cond), d, n);
    }
    std::string arm_LDREXD(Cond cond, Reg n, Reg d) {
        return fmt::format("ldrexd{} {}, {}, [{}]", CondToString(cond), d, d+1, n);
    }
    std::string arm_LDREXH(Cond cond, Reg n, Reg d) {
        return fmt::format("ldrexh{} {}, [{}]", CondToString(cond), d, n);
    }
    std::string arm_STREX(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("strex{} {}, {}, [{}]", CondToString(cond), d, m, n);
    }
    std::string arm_STREXB(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("strexb{} {}, {}, [{}]", CondToString(cond), d, m, n);
    }
    std::string arm_STREXD(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("strexd{} {}, {}, {}, [{}]", CondToString(cond), d, m, m+1, n);
    }
    std::string arm_STREXH(Cond cond, Reg n, Reg d, Reg m) {
        return fmt::format("strexh{} {}, {}, [{}]", CondToString(cond), d, m, n);
    }
    std::string arm_SWP(Cond cond, Reg n, Reg t, Reg t2) {
        return fmt::format("swp{} {}, {}, [{}]", CondToString(cond), t, t2, n);
    }
    std::string arm_SWPB(Cond cond, Reg n, Reg t, Reg t2) {
        return fmt::format("swpb{} {}, {}, [{}]", CondToString(cond), t, t2, n);
    }

    // Status register access instructions
    std::string arm_CPS() { return "ice"; }
    std::string arm_MRS(Cond cond, Reg d) {
        return fmt::format("mrs{} {}, apsr", CondToString(cond), d);
    }
    std::string arm_MSR_imm(Cond cond, int mask, int rotate, Imm8 imm8) {
        const bool write_c = Common::Bit<0>(mask);
        const bool write_x = Common::Bit<1>(mask);
        const bool write_s = Common::Bit<2>(mask);
        const bool write_f = Common::Bit<3>(mask);
        return fmt::format("msr{} cpsr_{}{}{}{}, #{}", CondToString(cond), write_c ? "c" : "", write_x ? "x" : "", write_s ? "s" : "", write_f ? "f" : "", ArmExpandImm(rotate, imm8));
    }
    std::string arm_MSR_reg(Cond cond, int mask, Reg n) {
        const bool write_c = Common::Bit<0>(mask);
        const bool write_x = Common::Bit<1>(mask);
        const bool write_s = Common::Bit<2>(mask);
        const bool write_f = Common::Bit<3>(mask);
        return fmt::format("msr{} cpsr_{}{}{}{}, {}", CondToString(cond), write_c ? "c" : "", write_x ? "x" : "", write_s ? "s" : "", write_f ? "f" : "", n);
    }
    std::string arm_RFE() { return "ice"; }
    std::string arm_SETEND(bool E) {
        return E ? "setend be" : "setend le";
    }
    std::string arm_SRS() { return "ice"; }

    // Floating point arithmetic instructions
    std::string vfp2_VADD(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vadd{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VSUB(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vsub{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VMUL(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vmul{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VMLA(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vmla{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VMLS(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vmls{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VNMUL(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vnmul{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VNMLA(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vnmla{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VNMLS(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vnmls{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VDIV(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
        return fmt::format("vdiv{}.{} {}, {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vn, N), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VMOV_u32_f64(Cond cond, size_t Vd, Reg t, bool D){
        return fmt::format("vmov{}.32 {}, {}", CondToString(cond), FPRegStr(true, Vd, D), t);
    }

    std::string vfp2_VMOV_f64_u32(Cond cond, size_t Vn, Reg t, bool N){
        return fmt::format("vmov{}.32 {}, {}", CondToString(cond), t, FPRegStr(true, Vn, N));
    }

    std::string vfp2_VMOV_u32_f32(Cond cond, size_t Vn, Reg t, bool N){
        return fmt::format("vmov{}.32 {}, {}", CondToString(cond), FPRegStr(false, Vn, N), t);
    }

    std::string vfp2_VMOV_f32_u32(Cond cond, size_t Vn, Reg t, bool N){
        return fmt::format("vmov{}.32 {}, {}", CondToString(cond), t, FPRegStr(false, Vn, N));
    }

    std::string vfp2_VMOV_2u32_2f32(Cond cond, Reg t2, Reg t, bool M, size_t Vm){
        return fmt::format("vmov{} {}, {}, {}, {}", CondToString(cond), FPRegStr(false, Vm, M), FPNextRegStr(false, Vm, M), t, t2);
    }

    std::string vfp2_VMOV_2f32_2u32(Cond cond, Reg t2, Reg t, bool M, size_t Vm){
        return fmt::format("vmov{} {}, {}, {}, {}", CondToString(cond), t, t2, FPRegStr(false, Vm, M), FPNextRegStr(false, Vm, M));
    }

    std::string vfp2_VMOV_2u32_f64(Cond cond, Reg t2, Reg t, bool M, size_t Vm){
        return fmt::format("vmov{} {}, {}, {}", CondToString(cond), FPRegStr(true, Vm, M), t, t2);
    }

    std::string vfp2_VMOV_f64_2u32(Cond cond, Reg t2, Reg t, bool M, size_t Vm){
        return fmt::format("vmov{} {}, {}, {}", CondToString(cond), t, t2, FPRegStr(true, Vm, M));
    }

    std::string vfp2_VMOV_reg(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm){
        return fmt::format("vmov{}.{} {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VABS(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
        return fmt::format("vadd{}.{} {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VNEG(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
        return fmt::format("vneg{}.{} {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VSQRT(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
        return fmt::format("vsqrt{}.{} {}, {}", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VCVT_f_to_f(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
        return fmt::format("vcvt{}.{}.{} {}, {}", CondToString(cond), !sz ? "f64" : "f32", sz ? "f64" : "f32", FPRegStr(!sz, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VCVT_to_float(Cond cond, bool D, size_t Vd, bool sz, bool is_signed, bool M, size_t Vm) {
        return fmt::format("vcvt{}.{}.{} {}, {}", CondToString(cond), sz ? "f64" : "f32", is_signed ? "s32" : "u32", FPRegStr(sz, Vd, D), FPRegStr(false, Vm, M));
    }

    std::string vfp2_VCVT_to_u32(Cond cond, bool D, size_t Vd, bool sz, bool round_towards_zero, bool M, size_t Vm) {
        return fmt::format("vcvt{}{}.u32.{} {}, {}", round_towards_zero ? "" : "r", CondToString(cond), sz ? "f64" : "f32", FPRegStr(false, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VCVT_to_s32(Cond cond, bool D, size_t Vd, bool sz, bool round_towards_zero, bool M, size_t Vm) {
        return fmt::format("vcvt{}{}.s32.{} {}, {}", round_towards_zero ? "" : "r", CondToString(cond), sz ? "f64" : "f32", FPRegStr(false, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VCMP(Cond cond, bool D, size_t Vd, bool sz, bool E, bool M, size_t Vm) {
        return fmt::format("vcmp{}{}.{} {}, {}", E ? "e" : "", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D), FPRegStr(sz, Vm, M));
    }

    std::string vfp2_VCMP_zero(Cond cond, bool D, size_t Vd, bool sz, bool E) {
        return fmt::format("vcmp{}{}.{} {}, #0.0", E ? "e" : "", CondToString(cond), sz ? "f64" : "f32", FPRegStr(sz, Vd, D));
    }

    std::string vfp2_VMSR(Cond cond, Reg t) {
        return fmt::format("vmsr{} fpscr, {}", CondToString(cond), t);
    }
    std::string vfp2_VMRS(Cond cond, Reg t) {
        if (t == Reg::R15) {
            return fmt::format("vmrs{} apsr_nzcv, fpscr", CondToString(cond));
        } else {
            return fmt::format("vmrs{} {}, fpscr", CondToString(cond), t);
        }
    }

    std::string vfp2_VPOP(Cond cond, bool D, size_t Vd, bool sz, Imm8 imm8) {
        return fmt::format("vpop{} {}(+{})", CondToString(cond), FPRegStr(sz, Vd, D), imm8 >> (sz ? 1 : 0));
    }

    std::string vfp2_VPUSH(Cond cond, bool D, size_t Vd, bool sz, Imm8 imm8) {
        return fmt::format("vpush{} {}(+{})", CondToString(cond), FPRegStr(sz, Vd, D), imm8 >> (sz ? 1 : 0));
    }

    std::string vfp2_VLDR(Cond cond, bool U, bool D, Reg n, size_t Vd, bool sz, Imm8 imm8) {
        u32 imm32 = imm8 << 2;
        return fmt::format("vldr{} {}, [{}, #{}{}]", CondToString(cond), FPRegStr(sz, Vd, D), n, U ? '+' : '-', imm32);
    }

    std::string vfp2_VSTR(Cond cond, bool U, bool D, Reg n, size_t Vd, bool sz, Imm8 imm8) {
        u32 imm32 = imm8 << 2;
        return fmt::format("vstr{} {}, [{}, #{}{}]", CondToString(cond), FPRegStr(sz, Vd, D), n, U ? '+' : '-', imm32);
    }

    std::string vfp2_VSTM_a1(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm8 imm8) {
        const char* mode = "<invalid mode>";
        if (!p && u) mode = "ia";
        if (p && !u) mode = "db";
        return fmt::format("vstm{}{}.f64 {}{}, {}(+{})", mode, CondToString(cond), n, w ? "!" : "", FPRegStr(true, Vd, D), imm8);
    }

    std::string vfp2_VSTM_a2(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm8 imm8) {
        const char* mode = "<invalid mode>";
        if (!p && u) mode = "ia";
        if (p && !u) mode = "db";
        return fmt::format("vstm{}{}.f32 {}{}, {}(+{})", mode, CondToString(cond), n, w ? "!" : "", FPRegStr(false, Vd, D), imm8);
    }

    std::string vfp2_VLDM_a1(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm8 imm8) {
        const char* mode = "<invalid mode>";
        if (!p && u) mode = "ia";
        if (p && !u) mode = "db";
        return fmt::format("vldm{}{}.f64 {}{}, {}(+{})", mode, CondToString(cond), n, w ? "!" : "", FPRegStr(true, Vd, D), imm8);
    }

    std::string vfp2_VLDM_a2(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm8 imm8) {
        const char* mode = "<invalid mode>";
        if (!p && u) mode = "ia";
        if (p && !u) mode = "db";
        return fmt::format("vldm{}{}.f32 {}{}, {}(+{})", mode, CondToString(cond), n, w ? "!" : "", FPRegStr(false, Vd, D), imm8);
    }

};

std::string DisassembleArm(u32 instruction) {
    DisassemblerVisitor visitor;
    if (auto vfp_decoder = DecodeVFP2<DisassemblerVisitor>(instruction)) {
        return vfp_decoder->get().call(visitor, instruction);
    } else if (auto decoder = DecodeArm<DisassemblerVisitor>(instruction)) {
        return decoder->get().call(visitor, instruction);
    } else {
        return fmt::format("UNKNOWN: {:x}", instruction);
    }
}

} // namespace Dynarmic::A32
