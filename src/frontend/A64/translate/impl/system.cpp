/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::HINT([[maybe_unused]] Imm<4> CRm, [[maybe_unused]] Imm<3> op2) {
    return true;
}

bool TranslatorVisitor::NOP() {
    return true;
}

bool TranslatorVisitor::YIELD() {
    return true;
}

bool TranslatorVisitor::WFE() {
    return true;
}

bool TranslatorVisitor::WFI() {
    return true;
}

bool TranslatorVisitor::SEV() {
    return true;
}

bool TranslatorVisitor::SEVL() {
    return true;
}

bool TranslatorVisitor::DSB(Imm<4> /*CRm*/) {
    ir.DataSynchronizationBarrier();
    return true;
}

bool TranslatorVisitor::DMB(Imm<4> /*CRm*/) {
    ir.DataMemoryBarrier();
    return true;
}

} // namespace Dynarmic::A64
