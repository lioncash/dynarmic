/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Dynarmic {

class Coprocessor;
class Jit;

/// These function pointers may be inserted into compiled code.
struct UserCallbacks {
    struct Memory {
        // All reads through this callback are 4-byte aligned.
        // Memory must be interpreted as little endian.
        std::uint32_t (*ReadCode)(std::uint32_t vaddr);

        // Reads through these callbacks may not be aligned.
        // Memory must be interpreted as if ENDIANSTATE == 0, endianness will be corrected by the JIT.
        std::uint8_t (*Read8)(std::uint32_t vaddr);
        std::uint16_t (*Read16)(std::uint32_t vaddr);
        std::uint32_t (*Read32)(std::uint32_t vaddr);
        std::uint64_t (*Read64)(std::uint32_t vaddr);

        // Writes through these callbacks may not be aligned.
        // Memory must be interpreted as if ENDIANSTATE == 0, endianness will be corrected by the JIT.
        void (*Write8)(std::uint32_t vaddr, std::uint8_t value);
        void (*Write16)(std::uint32_t vaddr, std::uint16_t value);
        void (*Write32)(std::uint32_t vaddr, std::uint32_t value);
        void (*Write64)(std::uint32_t vaddr, std::uint64_t value);

        // If this callback returns true, the JIT will assume MemoryRead* callbacks will always
        // return the same value at any point in time for this vaddr. The JIT may use this information
        // in optimizations.
        // An conservative implementation that always returns false is safe.
        bool (*IsReadOnlyMemory)(std::uint32_t vaddr);
    } memory = {};

    /// The intrepreter must execute only one instruction at PC.
    void (*InterpreterFallback)(std::uint32_t pc, Jit* jit, void* user_arg);
    void* user_arg = nullptr;

    // This callback is called whenever a SVC instruction is executed.
    void (*CallSVC)(std::uint32_t swi);

    // Timing-related callbacks
    void (*AddTicks)(std::uint64_t ticks);
    std::uint64_t (*GetTicksRemaining)();

    // Page Table
    // The page table is used for faster memory access. If an entry in the table is nullptr,
    // the JIT will fallback to calling the MemoryRead*/MemoryWrite* callbacks.
    static constexpr std::size_t PAGE_BITS = 12;
    static constexpr std::size_t NUM_PAGE_TABLE_ENTRIES = 1 << (32 - PAGE_BITS);
    std::array<std::uint8_t*, NUM_PAGE_TABLE_ENTRIES>* page_table = nullptr;

    // Coprocessors
    std::array<std::shared_ptr<Coprocessor>, 16> coprocessors;
};

} // namespace Dynarmic
