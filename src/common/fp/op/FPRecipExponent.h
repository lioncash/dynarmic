/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::FP {

class FPCR;
class FPSR;

template <typename FPT>
FPT FPRecipExponent(FPT op, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP
