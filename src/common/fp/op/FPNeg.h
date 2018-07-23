/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/fp/info.h"

namespace Dynarmic::FP {

template<typename FPT>
inline FPT FPNeg(FPT op) {
    return op ^ FPInfo<FPT>::sign_mask;
}

} // namespace Dynarmic::FP 
