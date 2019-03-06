/* This file is part of the dynarmic project.
 * Copyright (c) 2019 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

namespace Dynarmic::FP {

class FPCR;
class FPSR;
enum class RoundingMode;

template <typename FPT_TO, typename FPT_FROM>
FPT_TO FPConvert(FPT_FROM op, FPCR fpcr, RoundingMode rounding_mode, FPSR& fpsr);

} // namespace Dynarmic::FP
