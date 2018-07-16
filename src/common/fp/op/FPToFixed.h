/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/common_types.h"
#include "common/fp/fpsr.h"
#include "common/fp/rounding_mode.h"
#include "frontend/A64/FPCR.h"

namespace Dynarmic::FP {

using FPCR = A64::FPCR;

template<typename FPT>
u64 FPToFixed(size_t ibits, FPT op, size_t fbits, bool unsigned_, FPCR fpcr, RoundingMode rounding, FPSR& fpsr);

} // namespace Dynarmic::FP 
