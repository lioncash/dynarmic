/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/translate/impl/impl.h"

namespace Dynarmic::A64 {

static bool DataCacheInstruction(TranslatorVisitor& v, IREmitter& ir, DataCacheOperation op, const Reg Rt) {
    ir.DataCacheOperationRaised(op, v.X(64, Rt));
    return true;
}

bool TranslatorVisitor::DC_IVAC(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::InvalidateByVAToPoC, Rt);
}

bool TranslatorVisitor::DC_ISW(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::InvalidateBySetWay, Rt);
}

bool TranslatorVisitor::DC_CSW(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::CleanBySetWay, Rt);
}

bool TranslatorVisitor::DC_CISW(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::CleanAndInvalidateBySetWay, Rt);
}

bool TranslatorVisitor::DC_ZVA(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::ZeroByVA, Rt);
}

bool TranslatorVisitor::DC_CVAC(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::CleanByVAToPoC, Rt);
}

bool TranslatorVisitor::DC_CVAU(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::CleanByVAToPoU, Rt);
}

bool TranslatorVisitor::DC_CVAP(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::CleanByVAToPoP, Rt);
}

bool TranslatorVisitor::DC_CIVAC(Reg Rt) {
    return DataCacheInstruction(*this, ir, DataCacheOperation::CleanAndInvalidateByVAToPoC, Rt);
}

} // namespace Dynarmic::A64
