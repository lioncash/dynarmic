/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::B_cond(Imm<19> imm19, Cond cond) {
    s64 offset = concatenate(imm19, Imm<2>{0}).SignExtend<s64>();

    u64 target = ir.PC() + offset;
    auto cond_pass = IR::Term::LinkBlock{ir.current_location.SetPC(target)};
    auto cond_fail = IR::Term::LinkBlock{ir.current_location.AdvancePC(4)};
    ir.SetTerm(IR::Term::If{cond, cond_pass, cond_fail});
    return false;
}

bool TranslatorVisitor::B_uncond(Imm<26> imm26) {
    s64 offset = concatenate(imm26, Imm<2>{0}).SignExtend<s64>();

    u64 target = ir.PC() + offset;
    ir.SetTerm(IR::Term::LinkBlock{ir.current_location.SetPC(target)});
    return false;
}

bool TranslatorVisitor::BL(Imm<26> imm26) {
    s64 offset = concatenate(imm26, Imm<2>{0}).SignExtend<s64>();

    X(64, Reg::R30, ir.Imm64(ir.PC() + 4));
    ir.PushRSB(ir.current_location.AdvancePC(4));

    u64 target = ir.PC() + offset;
    ir.SetTerm(IR::Term::LinkBlock{ir.current_location.SetPC(target)});
    return false;
}

bool TranslatorVisitor::BLR(Reg Rn) {
    auto target = X(64, Rn);

    X(64, Reg::R30, ir.Imm64(ir.PC() + 4));
    ir.PushRSB(ir.current_location.AdvancePC(4));

    ir.SetPC(target);
    ir.SetTerm(IR::Term::ReturnToDispatch{});
    return false;
}

bool TranslatorVisitor::BR(Reg Rn) {
    auto target = X(64, Rn);

    ir.SetPC(target);
    ir.SetTerm(IR::Term::ReturnToDispatch{});
    return false;
}

bool TranslatorVisitor::RET(Reg Rn) {
    auto target = X(64, Rn);

    ir.SetPC(target);
    ir.SetTerm(IR::Term::PopRSBHint{});
    return false;
}

bool TranslatorVisitor::CBZ(bool sf, Imm<19> imm19, Reg Rt) {
    size_t datasize = sf ? 64 : 32;
    s64 offset = concatenate(imm19, Imm<2>{0}).SignExtend<s64>();

    IR::U32U64 operand1 = X(datasize, Rt);

    ir.SetCheckBit(ir.IsZero(operand1));

    u64 target = ir.PC() + offset;
    auto cond_pass = IR::Term::LinkBlock{ir.current_location.SetPC(target)};
    auto cond_fail = IR::Term::LinkBlock{ir.current_location.AdvancePC(4)};
    ir.SetTerm(IR::Term::CheckBit{cond_pass, cond_fail});
    return false;
}

bool TranslatorVisitor::CBNZ(bool sf, Imm<19> imm19, Reg Rt) {
    size_t datasize = sf ? 64 : 32;
    s64 offset = concatenate(imm19, Imm<2>{0}).SignExtend<s64>();

    IR::U32U64 operand1 = X(datasize, Rt);

    ir.SetCheckBit(ir.IsZero(operand1));

    u64 target = ir.PC() + offset;
    auto cond_pass = IR::Term::LinkBlock{ir.current_location.AdvancePC(4)};
    auto cond_fail = IR::Term::LinkBlock{ir.current_location.SetPC(target)};
    ir.SetTerm(IR::Term::CheckBit{cond_pass, cond_fail});
    return false;
}

bool TranslatorVisitor::TBZ(Imm<1> b5, Imm<5> b40, Imm<14> imm14, Reg Rt) {
    size_t datasize = b5 == 1 ? 64 : 32;
    u8 bit_pos = concatenate(b5, b40).ZeroExtend<u8>();
    s64 offset = concatenate(imm14, Imm<2>{0}).SignExtend<s64>();

    auto operand = X(datasize, Rt);

    ir.SetCheckBit(ir.TestBit(operand, ir.Imm8(bit_pos)));

    u64 target = ir.PC() + offset;
    auto cond_1 = IR::Term::LinkBlock{ir.current_location.AdvancePC(4)};
    auto cond_0 = IR::Term::LinkBlock{ir.current_location.SetPC(target)};
    ir.SetTerm(IR::Term::CheckBit{cond_1, cond_0});
    return false;
}

bool TranslatorVisitor::TBNZ(Imm<1> b5, Imm<5> b40, Imm<14> imm14, Reg Rt) {
    size_t datasize = b5 == 1 ? 64 : 32;
    u8 bit_pos = concatenate(b5, b40).ZeroExtend<u8>();
    s64 offset = concatenate(imm14, Imm<2>{0}).SignExtend<s64>();

    auto operand = X(datasize, Rt);

    ir.SetCheckBit(ir.TestBit(operand, ir.Imm8(bit_pos)));

    u64 target = ir.PC() + offset;
    auto cond_1 = IR::Term::LinkBlock{ir.current_location.SetPC(target)};
    auto cond_0 = IR::Term::LinkBlock{ir.current_location.AdvancePC(4)};
    ir.SetTerm(IR::Term::CheckBit{cond_1, cond_0});
    return false;
}

} // namespace Dynarmic::A64
