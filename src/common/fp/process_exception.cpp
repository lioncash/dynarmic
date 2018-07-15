/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "common/fp/fpsr.h"
#include "common/fp/process_exception.h"
#include "frontend/A64/FPCR.h"

namespace Dynarmic::FP {

void FPProcessException(FPExc exception, FPCR fpcr, FPSR& fpsr) {
    switch (exception) {
    case FPExc::InvalidOp:
        if (fpcr.IOE()) {
            UNIMPLEMENTED();
        }
        fpsr.IOC(true);
        break;
    case FPExc::DivideByZero:
        if (fpcr.DZE()) {
            UNIMPLEMENTED();
        }
        fpsr.DZC(true);
        break;
    case FPExc::Overflow:
        if (fpcr.OFE()) {
            UNIMPLEMENTED();
        }
        fpsr.OFC(true);
        break;
    case FPExc::Underflow:
        if (fpcr.UFE()) {
            UNIMPLEMENTED();
        }
        fpsr.UFC(true);
        break;
    case FPExc::Inexact:
        if (fpcr.IXE()) {
            UNIMPLEMENTED();
        }
        fpsr.IXC(true);
        break;
    case FPExc::InputDenorm:
        if (fpcr.IDE()) {
            UNIMPLEMENTED();
        }
        fpsr.IDC(true);
        break;
    default:
        UNREACHABLE();
        break;
    }
}

} // namespace Dynarmic::FP 
