/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::CFINV() {
    const IR::U32 nzcv = ir.GetNZCVRaw();
    const IR::U32 result = ir.Eor(nzcv, ir.Imm32(0x20000000));

    ir.SetNZCVRaw(result);
    return true;
}

} // namespace Dynarmic::A64
