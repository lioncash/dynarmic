/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::FP {

class FPCR;
class FPSR;

template<typename FPT>
FPT FPMulAdd(FPT addend, FPT op1, FPT op2, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP 
