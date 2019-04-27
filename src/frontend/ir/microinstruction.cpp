/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>

#include <fmt/ostream.h>

#include "common/assert.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"
#include "frontend/ir/type.h"

namespace Dynarmic::IR {

bool Inst::IsArithmeticShift() const {
    return op == Opcode::ArithmeticShiftRight32 ||
           op == Opcode::ArithmeticShiftRight64;
}

bool Inst::IsCircularShift() const {
    return op == Opcode::RotateRight32 ||
           op == Opcode::RotateRight64 ||
           op == Opcode::RotateRightExtended;
}

bool Inst::IsLogicalShift() const {
    switch (op) {
    case Opcode::LogicalShiftLeft32:
    case Opcode::LogicalShiftLeft64:
    case Opcode::LogicalShiftRight32:
    case Opcode::LogicalShiftRight64:
        return true;

    default:
        return false;
    }
}

bool Inst::IsShift() const {
    return IsArithmeticShift() ||
           IsCircularShift()   ||
           IsLogicalShift();
}

bool Inst::IsBarrier() const {
    switch (op) {
    case Opcode::A32DataMemoryBarrier:
    case Opcode::A32DataSynchronizationBarrier:
    case Opcode::A32InstructionSynchronizationBarrier:
    case Opcode::A64DataMemoryBarrier:
    case Opcode::A64DataSynchronizationBarrier:
    case Opcode::A64InstructionSynchronizationBarrier:
        return true;

    default:
        return false;
    }
}

bool Inst::IsSharedMemoryRead() const {
    switch (op) {
    case Opcode::A32ReadMemory8:
    case Opcode::A32ReadMemory16:
    case Opcode::A32ReadMemory32:
    case Opcode::A32ReadMemory64:
    case Opcode::A64ReadMemory8:
    case Opcode::A64ReadMemory16:
    case Opcode::A64ReadMemory32:
    case Opcode::A64ReadMemory64:
    case Opcode::A64ReadMemory128:
        return true;

    default:
        return false;
    }
}

bool Inst::IsSharedMemoryWrite() const {
    switch (op) {
    case Opcode::A32WriteMemory8:
    case Opcode::A32WriteMemory16:
    case Opcode::A32WriteMemory32:
    case Opcode::A32WriteMemory64:
    case Opcode::A64WriteMemory8:
    case Opcode::A64WriteMemory16:
    case Opcode::A64WriteMemory32:
    case Opcode::A64WriteMemory64:
    case Opcode::A64WriteMemory128:
        return true;

    default:
        return false;
    }
}

bool Inst::IsSharedMemoryReadOrWrite() const {
    return IsSharedMemoryRead() || IsSharedMemoryWrite();
}

bool Inst::IsExclusiveMemoryWrite() const {
    switch (op) {
    case Opcode::A32ExclusiveWriteMemory8:
    case Opcode::A32ExclusiveWriteMemory16:
    case Opcode::A32ExclusiveWriteMemory32:
    case Opcode::A32ExclusiveWriteMemory64:
    case Opcode::A64ExclusiveWriteMemory8:
    case Opcode::A64ExclusiveWriteMemory16:
    case Opcode::A64ExclusiveWriteMemory32:
    case Opcode::A64ExclusiveWriteMemory64:
    case Opcode::A64ExclusiveWriteMemory128:
        return true;

    default:
        return false;
    }
}

bool Inst::IsMemoryRead() const {
    return IsSharedMemoryRead();
}

bool Inst::IsMemoryWrite() const {
    return IsSharedMemoryWrite() || IsExclusiveMemoryWrite();
}

bool Inst::IsMemoryReadOrWrite() const {
    return IsMemoryRead() || IsMemoryWrite();
}

bool Inst::ReadsFromCPSR() const {
    switch (op) {
    case Opcode::A32GetCpsr:
    case Opcode::A32GetNFlag:
    case Opcode::A32GetZFlag:
    case Opcode::A32GetCFlag:
    case Opcode::A32GetVFlag:
    case Opcode::A32GetGEFlags:
    case Opcode::A64GetCFlag:
    case Opcode::A64GetNZCVRaw:
    case Opcode::ConditionalSelect32:
    case Opcode::ConditionalSelect64:
    case Opcode::ConditionalSelectNZCV:
        return true;

    default:
        return false;
    }
}

bool Inst::WritesToCPSR() const {
    switch (op) {
    case Opcode::A32SetCpsr:
    case Opcode::A32SetCpsrNZCV:
    case Opcode::A32SetCpsrNZCVQ:
    case Opcode::A32SetNFlag:
    case Opcode::A32SetZFlag:
    case Opcode::A32SetCFlag:
    case Opcode::A32SetVFlag:
    case Opcode::A32OrQFlag:
    case Opcode::A32SetGEFlags:
    case Opcode::A32SetGEFlagsCompressed:
    case Opcode::A64SetNZCVRaw:
    case Opcode::A64SetNZCV:
        return true;

    default:
        return false;
    }
}

bool Inst::WritesToSystemRegister() const {
    switch (op) {
    case Opcode::A64SetTPIDR:
        return true;
    default:
        return false;
    }
}

bool Inst::ReadsFromCoreRegister() const {
    switch (op) {
    case Opcode::A32GetRegister:
    case Opcode::A32GetExtendedRegister32:
    case Opcode::A32GetExtendedRegister64:
    case Opcode::A64GetW:
    case Opcode::A64GetX:
    case Opcode::A64GetS:
    case Opcode::A64GetD:
    case Opcode::A64GetQ:
    case Opcode::A64GetSP:
        return true;

    default:
        return false;
    }
}

bool Inst::WritesToCoreRegister() const {
    switch (op) {
    case Opcode::A32SetRegister:
    case Opcode::A32SetExtendedRegister32:
    case Opcode::A32SetExtendedRegister64:
    case Opcode::A32BXWritePC:
    case Opcode::A64SetW:
    case Opcode::A64SetX:
    case Opcode::A64SetS:
    case Opcode::A64SetD:
    case Opcode::A64SetQ:
    case Opcode::A64SetSP:
    case Opcode::A64SetPC:
        return true;

    default:
        return false;
    }
}

bool Inst::ReadsFromFPCR() const {
    switch (op) {
    case Opcode::A32GetFpscr:
    case Opcode::A32GetFpscrNZCV:
    case Opcode::A64GetFPCR:
        return true;

    default:
        return false;
    }
}

bool Inst::WritesToFPCR() const {
    switch (op) {
    case Opcode::A32SetFpscr:
    case Opcode::A32SetFpscrNZCV:
    case Opcode::A64SetFPCR:
        return true;

    default:
        return false;
    }
}

bool Inst::ReadsFromFPSR() const {
    return op == Opcode::A32GetFpscr                ||
           op == Opcode::A32GetFpscrNZCV            ||
           op == Opcode::A64GetFPSR                 ||
           ReadsFromFPSRCumulativeExceptionBits()   ||
           ReadsFromFPSRCumulativeSaturationBit();
}

bool Inst::WritesToFPSR() const {
    return op == Opcode::A32SetFpscr                ||
           op == Opcode::A32SetFpscrNZCV            ||
           op == Opcode::A64SetFPSR                 ||
           WritesToFPSRCumulativeExceptionBits()    ||
           WritesToFPSRCumulativeSaturationBit();
}

bool Inst::ReadsFromFPSRCumulativeExceptionBits() const {
    return ReadsFromAndWritesToFPSRCumulativeExceptionBits();
}

bool Inst::WritesToFPSRCumulativeExceptionBits() const {
    return ReadsFromAndWritesToFPSRCumulativeExceptionBits();
}

bool Inst::ReadsFromAndWritesToFPSRCumulativeExceptionBits() const {
    switch (op) {
    case Opcode::FPAdd32:
    case Opcode::FPAdd64:
    case Opcode::FPCompare32:
    case Opcode::FPCompare64:
    case Opcode::FPDiv32:
    case Opcode::FPDiv64:
    case Opcode::FPMax32:
    case Opcode::FPMax64:
    case Opcode::FPMaxNumeric32:
    case Opcode::FPMaxNumeric64:
    case Opcode::FPMin32:
    case Opcode::FPMin64:
    case Opcode::FPMinNumeric32:
    case Opcode::FPMinNumeric64:
    case Opcode::FPMul32:
    case Opcode::FPMul64:
    case Opcode::FPMulAdd16:
    case Opcode::FPMulAdd32:
    case Opcode::FPMulAdd64:
    case Opcode::FPRecipEstimate16:
    case Opcode::FPRecipEstimate32:
    case Opcode::FPRecipEstimate64:
    case Opcode::FPRecipExponent16:
    case Opcode::FPRecipExponent32:
    case Opcode::FPRecipExponent64:
    case Opcode::FPRecipStepFused16:
    case Opcode::FPRecipStepFused32:
    case Opcode::FPRecipStepFused64:
    case Opcode::FPRoundInt16:
    case Opcode::FPRoundInt32:
    case Opcode::FPRoundInt64:
    case Opcode::FPRSqrtEstimate16:
    case Opcode::FPRSqrtEstimate32:
    case Opcode::FPRSqrtEstimate64:
    case Opcode::FPRSqrtStepFused16:
    case Opcode::FPRSqrtStepFused32:
    case Opcode::FPRSqrtStepFused64:
    case Opcode::FPSqrt32:
    case Opcode::FPSqrt64:
    case Opcode::FPSub32:
    case Opcode::FPSub64:
    case Opcode::FPHalfToDouble:
    case Opcode::FPHalfToSingle:
    case Opcode::FPSingleToDouble:
    case Opcode::FPSingleToHalf:
    case Opcode::FPDoubleToHalf:
    case Opcode::FPDoubleToSingle:
    case Opcode::FPDoubleToFixedS32:
    case Opcode::FPDoubleToFixedS64:
    case Opcode::FPDoubleToFixedU32:
    case Opcode::FPDoubleToFixedU64:
    case Opcode::FPHalfToFixedS32:
    case Opcode::FPHalfToFixedS64:
    case Opcode::FPHalfToFixedU32:
    case Opcode::FPHalfToFixedU64:
    case Opcode::FPSingleToFixedS32:
    case Opcode::FPSingleToFixedS64:
    case Opcode::FPSingleToFixedU32:
    case Opcode::FPSingleToFixedU64:
    case Opcode::FPFixedU32ToSingle:
    case Opcode::FPFixedS32ToSingle:
    case Opcode::FPFixedU32ToDouble:
    case Opcode::FPFixedU64ToDouble:
    case Opcode::FPFixedU64ToSingle:
    case Opcode::FPFixedS32ToDouble:
    case Opcode::FPFixedS64ToDouble:
    case Opcode::FPFixedS64ToSingle:
    case Opcode::FPVectorAdd32:
    case Opcode::FPVectorAdd64:
    case Opcode::FPVectorDiv32:
    case Opcode::FPVectorDiv64:
    case Opcode::FPVectorEqual32:
    case Opcode::FPVectorEqual64:
    case Opcode::FPVectorFromSignedFixed32:
    case Opcode::FPVectorFromSignedFixed64:
    case Opcode::FPVectorFromUnsignedFixed32:
    case Opcode::FPVectorFromUnsignedFixed64:
    case Opcode::FPVectorGreater32:
    case Opcode::FPVectorGreater64:
    case Opcode::FPVectorGreaterEqual32:
    case Opcode::FPVectorGreaterEqual64:
    case Opcode::FPVectorMul32:
    case Opcode::FPVectorMul64:
    case Opcode::FPVectorMulAdd16:
    case Opcode::FPVectorMulAdd32:
    case Opcode::FPVectorMulAdd64:
    case Opcode::FPVectorPairedAddLower32:
    case Opcode::FPVectorPairedAddLower64:
    case Opcode::FPVectorPairedAdd32:
    case Opcode::FPVectorPairedAdd64:
    case Opcode::FPVectorRecipEstimate16:
    case Opcode::FPVectorRecipEstimate32:
    case Opcode::FPVectorRecipEstimate64:
    case Opcode::FPVectorRecipStepFused16:
    case Opcode::FPVectorRecipStepFused32:
    case Opcode::FPVectorRecipStepFused64:
    case Opcode::FPVectorRoundInt16:
    case Opcode::FPVectorRoundInt32:
    case Opcode::FPVectorRoundInt64:
    case Opcode::FPVectorRSqrtEstimate16:
    case Opcode::FPVectorRSqrtEstimate32:
    case Opcode::FPVectorRSqrtEstimate64:
    case Opcode::FPVectorRSqrtStepFused16:
    case Opcode::FPVectorRSqrtStepFused32:
    case Opcode::FPVectorRSqrtStepFused64:
    case Opcode::FPVectorSqrt32:
    case Opcode::FPVectorSqrt64:
    case Opcode::FPVectorSub32:
    case Opcode::FPVectorSub64:
    case Opcode::FPVectorToSignedFixed16:
    case Opcode::FPVectorToSignedFixed32:
    case Opcode::FPVectorToSignedFixed64:
    case Opcode::FPVectorToUnsignedFixed16:
    case Opcode::FPVectorToUnsignedFixed32:
    case Opcode::FPVectorToUnsignedFixed64:
        return true;

    default:
        return false;
    }
}

bool Inst::ReadsFromFPSRCumulativeSaturationBit() const {
    return false;
}

bool Inst::WritesToFPSRCumulativeSaturationBit() const {
    switch (op) {
    case Opcode::A64OrQC:
    case Opcode::VectorSignedSaturatedAbs8:
    case Opcode::VectorSignedSaturatedAbs16:
    case Opcode::VectorSignedSaturatedAbs32:
    case Opcode::VectorSignedSaturatedAbs64:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned8:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned16:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned32:
    case Opcode::VectorSignedSaturatedAccumulateUnsigned64:
    case Opcode::VectorSignedSaturatedDoublingMultiply16:
    case Opcode::VectorSignedSaturatedDoublingMultiply32:
    case Opcode::VectorSignedSaturatedDoublingMultiplyLong16:
    case Opcode::VectorSignedSaturatedDoublingMultiplyLong32:
    case Opcode::VectorSignedSaturatedNarrowToSigned16:
    case Opcode::VectorSignedSaturatedNarrowToSigned32:
    case Opcode::VectorSignedSaturatedNarrowToSigned64:
    case Opcode::VectorSignedSaturatedNarrowToUnsigned16:
    case Opcode::VectorSignedSaturatedNarrowToUnsigned32:
    case Opcode::VectorSignedSaturatedNarrowToUnsigned64:
    case Opcode::VectorSignedSaturatedNeg8:
    case Opcode::VectorSignedSaturatedNeg16:
    case Opcode::VectorSignedSaturatedNeg32:
    case Opcode::VectorSignedSaturatedNeg64:
    case Opcode::VectorSignedSaturatedShiftLeft8:
    case Opcode::VectorSignedSaturatedShiftLeft16:
    case Opcode::VectorSignedSaturatedShiftLeft32:
    case Opcode::VectorSignedSaturatedShiftLeft64:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned8:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned16:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned32:
    case Opcode::VectorSignedSaturatedShiftLeftUnsigned64:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned8:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned16:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned32:
    case Opcode::VectorUnsignedSaturatedAccumulateSigned64:
    case Opcode::VectorUnsignedSaturatedNarrow16:
    case Opcode::VectorUnsignedSaturatedNarrow32:
    case Opcode::VectorUnsignedSaturatedNarrow64:
    case Opcode::VectorUnsignedSaturatedShiftLeft8:
    case Opcode::VectorUnsignedSaturatedShiftLeft16:
    case Opcode::VectorUnsignedSaturatedShiftLeft32:
    case Opcode::VectorUnsignedSaturatedShiftLeft64:
        return true;

    default:
        return false;
    }
}

bool Inst::CausesCPUException() const {
    return op == Opcode::Breakpoint         ||
           op == Opcode::A32CallSupervisor  ||
           op == Opcode::A32ExceptionRaised ||
           op == Opcode::A64CallSupervisor  ||
           op == Opcode::A64ExceptionRaised;
}

bool Inst::AltersExclusiveState() const {
    return op == Opcode::A32ClearExclusive ||
           op == Opcode::A32SetExclusive   ||
           op == Opcode::A64ClearExclusive ||
           op == Opcode::A64SetExclusive   ||
           IsExclusiveMemoryWrite();
}

bool Inst::IsCoprocessorInstruction() const {
    switch (op) {
    case Opcode::A32CoprocInternalOperation:
    case Opcode::A32CoprocSendOneWord:
    case Opcode::A32CoprocSendTwoWords:
    case Opcode::A32CoprocGetOneWord:
    case Opcode::A32CoprocGetTwoWords:
    case Opcode::A32CoprocLoadWords:
    case Opcode::A32CoprocStoreWords:
        return true;

    default:
        return false;
    }
}

bool Inst::MayHaveSideEffects() const {
    return op == Opcode::PushRSB                        ||
           op == Opcode::A64SetCheckBit                 ||
           op == Opcode::A64DataCacheOperationRaised    ||
           IsBarrier()                                  ||
           CausesCPUException()                         ||
           WritesToCoreRegister()                       ||
           WritesToSystemRegister()                     ||
           WritesToCPSR()                               ||
           WritesToFPCR()                               ||
           WritesToFPSR()                               ||
           AltersExclusiveState()                       ||
           IsMemoryWrite()                              ||
           IsCoprocessorInstruction();
}

bool Inst::IsAPseudoOperation() const {
    switch (op) {
    case Opcode::GetCarryFromOp:
    case Opcode::GetOverflowFromOp:
    case Opcode::GetGEFromOp:
    case Opcode::GetNZCVFromOp:
    case Opcode::GetUpperFromOp:
    case Opcode::GetLowerFromOp:
        return true;

    default:
        return false;
    }
}

bool Inst::MayGetNZCVFromOp() const {
    switch (op) {
    case Opcode::Add32:
    case Opcode::Add64:
    case Opcode::Sub32:
    case Opcode::Sub64:
    case Opcode::And32:
    case Opcode::And64:
    case Opcode::Eor32:
    case Opcode::Eor64:
    case Opcode::Or32:
    case Opcode::Or64:
    case Opcode::Not32:
    case Opcode::Not64:
        return true;

    default:
        return false;
    }
}

bool Inst::AreAllArgsImmediates() const {
    return std::all_of(args.begin(), args.begin() + NumArgs(), [](const auto& value){ return value.IsImmediate(); });
}

bool Inst::HasAssociatedPseudoOperation() const {
    return carry_inst || overflow_inst || ge_inst || nzcv_inst || upper_inst || lower_inst;
}

Inst* Inst::GetAssociatedPseudoOperation(Opcode opcode) {
    // This is faster than doing a search through the block.
    switch (opcode) {
    case Opcode::GetCarryFromOp:
        ASSERT(!carry_inst || carry_inst->GetOpcode() == Opcode::GetCarryFromOp);
        return carry_inst;
    case Opcode::GetOverflowFromOp:
        ASSERT(!overflow_inst || overflow_inst->GetOpcode() == Opcode::GetOverflowFromOp);
        return overflow_inst;
    case Opcode::GetGEFromOp:
        ASSERT(!ge_inst || ge_inst->GetOpcode() == Opcode::GetGEFromOp);
        return ge_inst;
    case Opcode::GetNZCVFromOp:
        ASSERT(!nzcv_inst || nzcv_inst->GetOpcode() == Opcode::GetNZCVFromOp);
        return nzcv_inst;
    case Opcode::GetUpperFromOp:
        ASSERT(!upper_inst || upper_inst->GetOpcode() == Opcode::GetUpperFromOp);
        return upper_inst;
    case Opcode::GetLowerFromOp:
        ASSERT(!lower_inst || lower_inst->GetOpcode() == Opcode::GetLowerFromOp);
        return lower_inst;
    default:
        break;
    }

    ASSERT_MSG(false, "Not a valid pseudo-operation");
    return nullptr;
}

Type Inst::GetType() const {
    if (op == Opcode::Identity)
        return args[0].GetType();
    return GetTypeOf(op);
}

size_t Inst::NumArgs() const {
    return GetNumArgsOf(op);
}

Value Inst::GetArg(size_t index) const {
    ASSERT_MSG(index < GetNumArgsOf(op), "Inst::GetArg: index {} >= number of arguments of {} ({})", index, op, GetNumArgsOf(op));
    ASSERT_MSG(!args[index].IsEmpty() || GetArgTypeOf(op, index) == IR::Type::Opaque, "Inst::GetArg: index {} is empty", index, args[index].GetType());

    return args[index];
}

void Inst::SetArg(size_t index, Value value) {
    ASSERT_MSG(index < GetNumArgsOf(op), "Inst::SetArg: index {} >= number of arguments of {} ({})", index, op, GetNumArgsOf(op));
    ASSERT_MSG(AreTypesCompatible(value.GetType(), GetArgTypeOf(op, index)), "Inst::SetArg: type {} of argument {} not compatible with operation {} ({})", value.GetType(), index, op, GetArgTypeOf(op, index));

    if (!args[index].IsImmediate()) {
        UndoUse(args[index]);
    }
    if (!value.IsImmediate()) {
        Use(value);
    }

    args[index] = value;
}

void Inst::Invalidate() {
    ClearArgs();
    op = Opcode::Void;
}

void Inst::ClearArgs() {
    for (auto& value : args) {
        if (!value.IsImmediate()) {
            UndoUse(value);
        }
        value = {};
    }
}

void Inst::ReplaceUsesWith(Value replacement) {
    Invalidate();

    op = Opcode::Identity;

    if (!replacement.IsImmediate()) {
        Use(replacement);
    }

    args[0] = replacement;
}

void Inst::Use(const Value& value) {
    value.GetInst()->use_count++;

    switch (op){
    case Opcode::GetCarryFromOp:
        ASSERT_MSG(!value.GetInst()->carry_inst, "Only one of each type of pseudo-op allowed");
        value.GetInst()->carry_inst = this;
        break;
    case Opcode::GetOverflowFromOp:
        ASSERT_MSG(!value.GetInst()->overflow_inst, "Only one of each type of pseudo-op allowed");
        value.GetInst()->overflow_inst = this;
        break;
    case Opcode::GetGEFromOp:
        ASSERT_MSG(!value.GetInst()->ge_inst, "Only one of each type of pseudo-op allowed");
        value.GetInst()->ge_inst = this;
        break;
    case Opcode::GetNZCVFromOp:
        ASSERT_MSG(!value.GetInst()->nzcv_inst, "Only one of each type of pseudo-op allowed");
        ASSERT_MSG(value.GetInst()->MayGetNZCVFromOp(), "This value doesn't support the GetNZCVFromOp pseduo-op");
        value.GetInst()->nzcv_inst = this;
        break;
    case Opcode::GetUpperFromOp:
        ASSERT_MSG(!value.GetInst()->upper_inst, "Only one of each type of pseudo-op allowed");
        value.GetInst()->upper_inst = this;
        break;
    case Opcode::GetLowerFromOp:
        ASSERT_MSG(!value.GetInst()->lower_inst, "Only one of each type of pseudo-op allowed");
        value.GetInst()->lower_inst = this;
        break;
    default:
        break;
    }
}

void Inst::UndoUse(const Value& value) {
    value.GetInst()->use_count--;

    switch (op){
    case Opcode::GetCarryFromOp:
        ASSERT(value.GetInst()->carry_inst->GetOpcode() == Opcode::GetCarryFromOp);
        value.GetInst()->carry_inst = nullptr;
        break;
    case Opcode::GetOverflowFromOp:
        ASSERT(value.GetInst()->overflow_inst->GetOpcode() == Opcode::GetOverflowFromOp);
        value.GetInst()->overflow_inst = nullptr;
        break;
    case Opcode::GetGEFromOp:
        ASSERT(value.GetInst()->ge_inst->GetOpcode() == Opcode::GetGEFromOp);
        value.GetInst()->ge_inst = nullptr;
        break;
    case Opcode::GetNZCVFromOp:
        ASSERT(value.GetInst()->nzcv_inst->GetOpcode() == Opcode::GetNZCVFromOp);
        value.GetInst()->nzcv_inst = nullptr;
        break;
    case Opcode::GetUpperFromOp:
        ASSERT(value.GetInst()->upper_inst->GetOpcode() == Opcode::GetUpperFromOp);
        value.GetInst()->upper_inst = nullptr;
        break;
    case Opcode::GetLowerFromOp:
        ASSERT(value.GetInst()->lower_inst->GetOpcode() == Opcode::GetLowerFromOp);
        value.GetInst()->lower_inst = nullptr;
        break;
    default:
        break;
    }
}

} // namespace Dynarmic::IR
