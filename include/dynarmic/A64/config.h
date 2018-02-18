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

enum class DataCacheOperation {
    /// DC CISW
    CleanAndInvalidateBySetWay,
    /// DC CIVAC
    CleanAndInvalidateByVAToPoC,
    /// DC CSW
    CleanBySetWay,
    /// DC CVAC
    CleanByVAToPoC,
    /// DC CVAU
    CleanByVAToPoU,
    /// DC CVAP
    CleanByVAToPoP,
    /// DC ISW
    InvalidateBySetWay,
    /// DC IVAC
    InvaldiateByVAToPoC,
    /// DC ZVA
    ZeroByVA,
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
    virtual void DataCacheOperationRaised(DataCacheOperation /*op*/, VAddr /*value*/) {}

    // Timing-related callbacks
    // ticks ticks have passed
    virtual void AddTicks(std::uint64_t ticks) = 0;
    // How many more ticks am I allowed to execute?
    virtual std::uint64_t GetTicksRemaining() = 0;
};

struct UserConfig {
    UserCallbacks* callbacks;

    /// When set to true, UserCallbacks::DataCacheOperationRaised will be called when any
    /// data cache instruction is executed. Notably DC ZVA will not implicitly do anything.
    /// When set to false, UserCallbacks::DataCacheOperationRaised will never be called.
    /// Executing DC ZVA in this mode will result in zeros being written to memory.
    bool hook_data_cache_operations = false;

    /// DCZID_EL0<3:0> is log2 of the block size in words
    /// DCZID_EL0<4> is 0 if the DC ZVA instruction is permitted.
    std::uint32_t dczid_el0 = 4;

    /// Pointer to where TPIDRRO_EL0 is stored. This pointer will be inserted into
    /// emitted code.
    const std::uint64_t* tpidrro_el0 = nullptr;

    /// Pointer to the page table which we can use for direct page table access.
    /// If an entry in page_table is null, the relevant memory callback will be called.
    /// If page_table is nullptr, all memory accesses hit the memory callbacks.
    void** page_table = nullptr;
    /// Declares how many valid address bits are there in virtual addresses.
    /// Determines the size of page_table. Valid values are between 12 and 64 inclusive.
    /// This is only used if page_table is not nullptr.
    size_t page_table_address_space_bits = 36;
    /// Determines what happens if the guest accesses an entry that is off the end of the
    /// page table. If true, Dynarmic will silently mirror page_table's address space. If
    /// false, accessing memory outside of page_table bounds will result in a call to the
    /// relevant memory callback.
    /// This is only used if page_table is not nullptr.
    bool silently_mirror_page_table = true;

    // The below options relate to accuracy of floating-point emulation.

    /// Determines how accurate NaN handling is.
    enum class NaNAccuracy {
        /// Results of operations with NaNs will exactly match hardware.
        Accurate,
        /// Behave as if FPCR.DN is always set.
        AlwaysForceDefaultNaN,
        /// No special handling of NaN, other than setting default NaN when FPCR.DN is set.
        NoChecks,
    } floating_point_nan_accuracy = NaNAccuracy::Accurate;

    // Determines whether AddTicks and GetTicksRemaining are called.
    // If false, execution will continue until soon after Jit::HaltExecution is called.
    // bool enable_ticks = true; // TODO
};

} // namespace A64
} // namespace Dynarmic
