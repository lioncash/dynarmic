/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::FP {

struct FPUnpacked;

FPUnpacked FusedMulAdd(FPUnpacked addend, FPUnpacked op1, FPUnpacked op2);

} // namespace Dynarmic::FP
