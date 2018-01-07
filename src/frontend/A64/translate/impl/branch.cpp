/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic {
namespace A64 {

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

} // namespace A64
} // namespace Dynarmic
