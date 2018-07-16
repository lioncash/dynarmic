/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "common/bit_util.h"
#include "common/fp/fpsr.h"
#include "common/fp/info.h"
#include "common/fp/process_exception.h"
#include "common/fp/process_nan.h"
#include "frontend/A64/FPCR.h"

namespace Dynarmic::FP {

template<typename FPT>
FPT FPProcessNaN(FPType type, FPT op, FPCR fpcr, FPSR& fpsr) {
    ASSERT(type == FPType::QNaN || type == FPType::SNaN);

    constexpr size_t topfrac = FPInfo<FPT>::explicit_mantissa_width - 1;

    FPT result = op;

    if (type == FPType::SNaN) {
        result = Common::ModifyBit<topfrac>(op, true);
        FPProcessException(FPExc::InvalidOp, fpcr, fpsr);
    }

    if (fpcr.DN()) {
        result = FPInfo<FPT>::DefaultNaN();
    }

    return result;
}

template u32 FPProcessNaN<u32>(FPType type, u32 op, FPCR fpcr, FPSR& fpsr);
template u64 FPProcessNaN<u64>(FPType type, u64 op, FPCR fpcr, FPSR& fpsr);

} // namespace Dynarmic::FP 
