/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Dynarmic {
namespace A64 {

using VAddr = std::uint64_t;

using Vector = std::array<std::uint64_t, 2>;
static_assert(sizeof(Vector) == sizeof(std::uint64_t) * 2, "Vector must be 128 bits in size");

enum class Exception {
    /// An UndefinedFault occured due to executing instruction with an unallocated encoding
    UnallocatedEncoding,
    /// An UndefinedFault occured due to executing instruction containing a reserved value
    ReservedValue,
    /// An unpredictable instruction is to be executed. Implementation-defined behaviour should now happen.
    /// This behaviour is up to the user of this library to define.
    /// Note: Constraints on unpredictable behaviour are specified in the ARMv8 ARM.
    UnpredictableInstruction,
};

struct UserCallbacks {
    virtual ~UserCallbacks() = default;

    // All reads through this callback are 4-byte aligned.
    // Memory must be interpreted as little endian.
    virtual std::uint32_t MemoryReadCode(VAddr vaddr) { return MemoryRead32(vaddr); }

    // Reads through these callbacks may not be aligned.
    virtual std::uint8_t MemoryRead8(VAddr vaddr) = 0;
    virtual std::uint16_t MemoryRead16(VAddr vaddr) = 0;
    virtual std::uint32_t MemoryRead32(VAddr vaddr) = 0;
    virtual std::uint64_t MemoryRead64(VAddr vaddr) = 0;
    virtual Vector MemoryRead128(VAddr vaddr) = 0;

    // Writes through these callbacks may not be aligned.
    virtual void MemoryWrite8(VAddr vaddr, std::uint8_t value) = 0;
    virtual void MemoryWrite16(VAddr vaddr, std::uint16_t value) = 0;
    virtual void MemoryWrite32(VAddr vaddr, std::uint32_t value) = 0;
    virtual void MemoryWrite64(VAddr vaddr, std::uint64_t value) = 0;
    virtual void MemoryWrite128(VAddr vaddr, Vector value) = 0;

    // If this callback returns true, the JIT will assume MemoryRead* callbacks will always
    // return the same value at any point in time for this vaddr. The JIT may use this information
    // in optimizations.
    // A conservative implementation that always returns false is safe.
    virtual bool IsReadOnlyMemory(VAddr /* vaddr */) { return false; }

    /// The intrepreter must execute exactly num_instructions starting from PC.
    virtual void InterpreterFallback(VAddr pc, size_t num_instructions) = 0;

    // This callback is called whenever a SVC instruction is executed.
    virtual void CallSVC(std::uint32_t swi) = 0;

    virtual void ExceptionRaised(VAddr pc, Exception exception) = 0;

    // Timing-related callbacks
    // ticks ticks have passed
    virtual void AddTicks(std::uint64_t ticks) = 0;
    // How many more ticks am I allowed to execute?
    virtual std::uint64_t GetTicksRemaining() = 0;
};

struct UserConfig {
    UserCallbacks* callbacks;

    // Determines whether AddTicks and GetTicksRemaining are called.
    // If false, execution will continue until soon after Jit::HaltExecution is called.
    // bool enable_ticks = true; // TODO
};

} // namespace A64
} // namespace Dynarmic
