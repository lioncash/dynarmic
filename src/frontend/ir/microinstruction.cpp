/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>

#include "common/assert.h"
#include "frontend/ir/microinstruction.h"

namespace Dynarmic {
namespace IR {

bool Inst::IsArithmeticShift() const {
    return op == Opcode::ArithmeticShiftRight;
}

bool Inst::IsCircularShift() const {
    return op == Opcode::RotateRight ||
           op == Opcode::RotateRightExtended;
}

bool Inst::IsLogicalShift() const {
    switch (op) {
    case Opcode::LogicalShiftLeft:
    case Opcode::LogicalShiftRight:
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

bool Inst::IsSharedMemoryRead() const {
    switch (op) {
    case Opcode::ReadMemory8:
    case Opcode::ReadMemory16:
    case Opcode::ReadMemory32:
    case Opcode::ReadMemory64:
        return true;

    default:
        return false;
    }
}

bool Inst::IsSharedMemoryWrite() const {
    switch (op) {
    case Opcode::WriteMemory8:
    case Opcode::WriteMemory16:
    case Opcode::WriteMemory32:
    case Opcode::WriteMemory64:
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
    case Opcode::ExclusiveWriteMemory8:
    case Opcode::ExclusiveWriteMemory16:
    case Opcode::ExclusiveWriteMemory32:
    case Opcode::ExclusiveWriteMemory64:
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
    case Opcode::GetCpsr:
    case Opcode::GetNFlag:
    case Opcode::GetZFlag:
    case Opcode::GetCFlag:
    case Opcode::GetVFlag:
    case Opcode::GetGEFlags:
        return true;

    default:
        return false;
    }
}

bool Inst::WritesToCPSR() const {
    switch (op) {
    case Opcode::SetCpsr:
    case Opcode::SetCpsrNZCV:
    case Opcode::SetCpsrNZCVQ:
    case Opcode::SetNFlag:
    case Opcode::SetZFlag:
    case Opcode::SetCFlag:
    case Opcode::SetVFlag:
    case Opcode::OrQFlag:
    case Opcode::SetGEFlags:
    case Opcode::SetGEFlagsCompressed:
        return true;

    default:
        return false;
    }
}

bool Inst::ReadsFromCoreRegister() const {
    switch (op) {
    case Opcode::GetRegister:
    case Opcode::GetExtendedRegister32:
    case Opcode::GetExtendedRegister64:
        return true;

    default:
        return false;
    }
}

bool Inst::WritesToCoreRegister() const {
    switch (op) {
    case Opcode::SetRegister:
    case Opcode::SetExtendedRegister32:
    case Opcode::SetExtendedRegister64:
    case Opcode::BXWritePC:
        return true;

    default:
        return false;
    }
}

bool Inst::ReadsFromFPSCR() const {
    switch (op) {
    case Opcode::GetFpscr:
    case Opcode::GetFpscrNZCV:
    case Opcode::FPAbs32:
    case Opcode::FPAbs64:
    case Opcode::FPAdd32:
    case Opcode::FPAdd64:
    case Opcode::FPCompare32:
    case Opcode::FPCompare64:
    case Opcode::FPDiv32:
    case Opcode::FPDiv64:
    case Opcode::FPMul32:
    case Opcode::FPMul64:
    case Opcode::FPNeg32:
    case Opcode::FPNeg64:
    case Opcode::FPSqrt32:
    case Opcode::FPSqrt64:
    case Opcode::FPSub32:
    case Opcode::FPSub64:
        return true;

    default:
        return false;
    }
}

bool Inst::WritesToFPSCR() const {
    switch (op) {
    case Opcode::SetFpscr:
    case Opcode::SetFpscrNZCV:
    case Opcode::FPAbs32:
    case Opcode::FPAbs64:
    case Opcode::FPAdd32:
    case Opcode::FPAdd64:
    case Opcode::FPCompare32:
    case Opcode::FPCompare64:
    case Opcode::FPDiv32:
    case Opcode::FPDiv64:
    case Opcode::FPMul32:
    case Opcode::FPMul64:
    case Opcode::FPNeg32:
    case Opcode::FPNeg64:
    case Opcode::FPSqrt32:
    case Opcode::FPSqrt64:
    case Opcode::FPSub32:
    case Opcode::FPSub64:
        return true;

    default:
        return false;
    }
}

bool Inst::CausesCPUException() const {
    return op == Opcode::Breakpoint ||
           op == Opcode::CallSupervisor;
}

bool Inst::AltersExclusiveState() const {
    return op == Opcode::ClearExclusive ||
           op == Opcode::SetExclusive   ||
           IsExclusiveMemoryWrite();
}

bool Inst::IsCoprocessorInstruction() const {
    switch (op) {
    case Opcode::CoprocInternalOperation:
    case Opcode::CoprocSendOneWord:
    case Opcode::CoprocSendTwoWords:
    case Opcode::CoprocGetOneWord:
    case Opcode::CoprocGetTwoWords:
    case Opcode::CoprocLoadWords:
    case Opcode::CoprocStoreWords:
        return true;

    default:
        return false;
    }
}

bool Inst::MayHaveSideEffects() const {
    return op == Opcode::PushRSB  ||
           CausesCPUException()   ||
           WritesToCoreRegister() ||
           WritesToCPSR()         ||
           WritesToFPSCR()        ||
           AltersExclusiveState() ||
           IsMemoryWrite()        ||
           IsCoprocessorInstruction();
}

bool Inst::AreAllArgsImmediates() const {
    return std::all_of(args.begin(), args.begin() + NumArgs(), [](const auto& value){ return value.IsImmediate(); });
}

bool Inst::HasAssociatedPseudoOperation() const {
    return carry_inst || overflow_inst || ge_inst;
}

Inst* Inst::GetAssociatedPseudoOperation(Opcode opcode) {
    // This is faster than doing a search through the block.
    switch (opcode) {
    case IR::Opcode::GetCarryFromOp:
        ASSERT(!carry_inst || carry_inst->GetOpcode() == Opcode::GetCarryFromOp);
        return carry_inst;
    case IR::Opcode::GetOverflowFromOp:
        ASSERT(!overflow_inst || overflow_inst->GetOpcode() == Opcode::GetOverflowFromOp);
        return overflow_inst;
    case IR::Opcode::GetGEFromOp:
        ASSERT(!ge_inst || ge_inst->GetOpcode() == Opcode::GetGEFromOp);
        return ge_inst;
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

Value Inst::GetArg(size_t index) const {
    ASSERT(index < GetNumArgsOf(op));
    ASSERT(!args[index].IsEmpty());

    return args[index];
}

void Inst::SetArg(size_t index, Value value) {
    ASSERT(index < GetNumArgsOf(op));
    ASSERT(AreTypesCompatible(value.GetType(), GetArgTypeOf(op, index)));

    if (!args[index].IsImmediate()) {
        UndoUse(args[index]);
    }
    if (!value.IsImmediate()) {
        Use(value);
    }

    args[index] = value;
}

void Inst::Invalidate() {
    for (auto& value : args) {
        if (!value.IsImmediate()) {
            UndoUse(value);
        }
        value = {};
    }
    op = Opcode::Void;
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
    default:
        break;
    }
}

} // namespace IR
} // namespace Dynarmic
