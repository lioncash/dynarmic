/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/fp/fpcr.h"
#include "common/fp/fpsr.h"
#include "common/fp/unpacked.h"

namespace Dynarmic::FP {

template<typename FPT>
FPT FPProcessNaN(FPType type, FPT op, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP 
