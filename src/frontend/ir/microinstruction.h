/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>

#include "common/common_types.h"
#include "common/intrusive_list.h"
#include "frontend/ir/value.h"

namespace Dynarmic::IR {

enum class Opcode;
enum class Type;

/**
 * A representation of a microinstruction. A single ARM/Thumb instruction may be
 * converted into zero or more microinstructions.
 */
class Inst final : public Common::IntrusiveListNode<Inst> {
public:
    explicit Inst(Opcode op) : op(op) {}

    /// Determines whether or not this instruction performs an arithmetic shift.
    bool IsArithmeticShift() const;
    /// Determines whether or not this instruction performs a logical shift.
    bool IsLogicalShift() const;
    /// Determines whether or not this instruction performs a circular shift.
    bool IsCircularShift() const;
    /// Determines whether or not this instruction performs any kind of shift.
    bool IsShift() const;

    /// Determines whether or not this instruction performs a shared memory read.
    bool IsSharedMemoryRead() const;
    /// Determines whether or not this instruction performs a shared memory write.
    bool IsSharedMemoryWrite() const;
    /// Determines whether or not this instruction performs a shared memory read or write.
    bool IsSharedMemoryReadOrWrite() const;
    /// Determines whether or not this instruction performs an atomic memory write.
    bool IsExclusiveMemoryWrite() const;

    /// Determines whether or not this instruction performs any kind of memory read.
    bool IsMemoryRead() const;
    /// Determines whether or not this instruction performs any kind of memory write.
    bool IsMemoryWrite() const;
    /// Determines whether or not this instruction performs any kind of memory access.
    bool IsMemoryReadOrWrite() const;

    /// Determines whether or not this instruction reads from the CPSR.
    bool ReadsFromCPSR() const;
    /// Determines whether or not this instruction writes to the CPSR.
    bool WritesToCPSR() const;

    /// Determines whether or not this instruction writes to a system register.
    bool WritesToSystemRegister() const;

    /// Determines whether or not this instruction reads from a core register.
    bool ReadsFromCoreRegister() const;
    /// Determines whether or not this instruction writes to a core register.
    bool WritesToCoreRegister() const;

    /// Determines whether or not this instruction reads from the FPSCR.
    bool ReadsFromFPSCR() const;
    /// Determines whether or not this instruction writes to the FPSCR.
    bool WritesToFPSCR() const;

    /// Determines whether or not this instruction alters memory-exclusivity.
    bool AltersExclusiveState() const;

    /// Determines whether or not this instruction accesses a coprocessor.
    bool IsCoprocessorInstruction() const;

    /// Determines whether or not this instruction causes a CPU exception.
    bool CausesCPUException() const;

    /// Determines whether or not this instruction may have side-effects.
    bool MayHaveSideEffects() const;

    /// Determines whether or not this instruction is a pseduo-instruction.
    /// Pseudo-instructions depend on their parent instructions for their semantics.
    bool IsAPseudoOperation() const;

    /// Determins whether or not this instruction supports the GetNZCVFromOp pseudo-operation.
    bool MayGetNZCVFromOp() const;

    /// Determines if all arguments of this instruction are immediates.
    bool AreAllArgsImmediates() const;

    size_t UseCount() const { return use_count; }
    bool HasUses() const { return use_count > 0; }

    /// Determines if there is a pseudo-operation associated with this instruction.
    bool HasAssociatedPseudoOperation() const;
    /// Gets a pseudo-operation associated with this instruction.
    Inst* GetAssociatedPseudoOperation(Opcode opcode);

    /// Get the microop this microinstruction represents.
    Opcode GetOpcode() const { return op; }
    /// Get the type this instruction returns.
    Type GetType() const;
    /// Get the number of arguments this instruction has.
    size_t NumArgs() const;

    Value GetArg(size_t index) const;
    void SetArg(size_t index, Value value);

    void Invalidate();
    void ClearArgs();

    void ReplaceUsesWith(Value replacement);

private:
    void Use(const Value& value);
    void UndoUse(const Value& value);

    Opcode op;
    size_t use_count = 0;
    std::array<Value, 3> args;

    // Pointers to related pseudooperations:
    // Since not all combinations are possible, we use a union to save space
    union {
        Inst* carry_inst = nullptr;
        Inst* ge_inst;
    };
    Inst* overflow_inst = nullptr;
    Inst* nzcv_inst = nullptr;
};

} // namespace Dynarmic::IR
