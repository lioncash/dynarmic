/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <dynarmic/A32/a32.h>

#include "common/assert.h"
#include "common/common_types.h"

template <typename InstructionType_, u32 infinite_loop>
class A32TestEnv final : public Dynarmic::A32::UserCallbacks {
public:
    using InstructionType = InstructionType_;
    using RegisterArray = std::array<u32, 16>;
    using ExtRegsArray = std::array<u32, 64>;

    u64 ticks_left = 0;
    bool code_mem_modified_by_guest = false;
    std::vector<InstructionType> code_mem;
    std::map<u32, u8> modified_memory;
    std::vector<std::string> interrupts;

    std::uint32_t MemoryReadCode(u32 vaddr) override {
        const size_t index = vaddr / sizeof(InstructionType);
        if (index < code_mem.size()) {
            u32 value;
            std::memcpy(&value, &code_mem[index], sizeof(u32));
            return value;
        }
        return infinite_loop; // B .
    }

    std::uint8_t MemoryRead8(u32 vaddr) override {
        if (vaddr < sizeof(InstructionType) * code_mem.size()) {
            return reinterpret_cast<u8*>(code_mem.data())[vaddr];
        }
        if (auto iter = modified_memory.find(vaddr); iter != modified_memory.end()) {
            return iter->second;
        }
        return static_cast<u8>(vaddr);
    }
    std::uint16_t MemoryRead16(u32 vaddr) override {
        return u16(MemoryRead8(vaddr)) | u16(MemoryRead8(vaddr + 1)) << 8;
    }
    std::uint32_t MemoryRead32(u32 vaddr) override {
        return u32(MemoryRead16(vaddr)) | u32(MemoryRead16(vaddr + 2)) << 16;
    }
    std::uint64_t MemoryRead64(u32 vaddr) override {
        return u64(MemoryRead32(vaddr)) | u64(MemoryRead32(vaddr + 4)) << 32;
    }

    void MemoryWrite8(u32 vaddr, std::uint8_t value) override {
        if (vaddr < code_mem.size() * sizeof(u32)) {
            code_mem_modified_by_guest = true;
        }
        modified_memory[vaddr] = value;
    }
    void MemoryWrite16(u32 vaddr, std::uint16_t value) override {
        MemoryWrite8(vaddr, static_cast<u8>(value));
        MemoryWrite8(vaddr + 1, static_cast<u8>(value >> 8));
    }
    void MemoryWrite32(u32 vaddr, std::uint32_t value) override {
        MemoryWrite16(vaddr, static_cast<u16>(value));
        MemoryWrite16(vaddr + 2, static_cast<u16>(value >> 16));
    }
    void MemoryWrite64(u32 vaddr, std::uint64_t value) override {
        MemoryWrite32(vaddr, static_cast<u32>(value));
        MemoryWrite32(vaddr + 4, static_cast<u32>(value >> 32));
    }

    void InterpreterFallback(u32 pc, size_t num_instructions) override { ASSERT_MSG(false, "InterpreterFallback({:08x}, {})", pc, num_instructions); }

    void CallSVC(std::uint32_t swi) override { ASSERT_MSG(false, "CallSVC({})", swi); }

    void ExceptionRaised(u32 pc, Dynarmic::A32::Exception /*exception*/) override { ASSERT_MSG(false, "ExceptionRaised({:08x})", pc); }

    void AddTicks(std::uint64_t ticks) override {
        if (ticks > ticks_left) {
            ticks_left = 0;
            return;
        }
        ticks_left -= ticks;
    }
    std::uint64_t GetTicksRemaining() override {
        return ticks_left;
    }
};

using ArmTestEnv = A32TestEnv<u32, 0xEAFFFFFE>;
using ThumbTestEnv = A32TestEnv<u16, 0xE7FEE7FE>;
