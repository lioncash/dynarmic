/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <xbyak.h>

#include "backend/x64/hostloc.h"

namespace Dynarmic::BackendX64 {

Xbyak::Reg64 HostLocToReg64(HostLoc loc) {
    ASSERT(HostLocIsGPR(loc));
    return Xbyak::Reg64(static_cast<int>(loc));
}

Xbyak::Xmm HostLocToXmm(HostLoc loc) {
    ASSERT(HostLocIsXMM(loc));
    return Xbyak::Xmm(static_cast<int>(loc) - static_cast<int>(HostLoc::XMM0));
}

} // namespace Dynarmic::BackendX64
