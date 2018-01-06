/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include <catch.hpp>

#include <dynarmic/A64/a64.h>

#include "common/assert.h"
#include "common/common_types.h"

class TestEnv final : public Dynarmic::A64::UserCallbacks {
    u64 ticks_left = 0;
    std::array<u32, 3000> code_mem{};

    std::uint32_t MemoryReadCode(u64 vaddr) override {
        if (vaddr < code_mem.size() * sizeof(u32)) {
            size_t index = vaddr / sizeof(u32);
            return code_mem[index];
        }
        ASSERT_MSG(false, "MemoryReadCode(%llx)", vaddr);
    }

    std::uint8_t MemoryRead8(u64 vaddr) override { ASSERT_MSG(false, "MemoryRead8(%llx)", vaddr); }
    std::uint16_t MemoryRead16(u64 vaddr) override { ASSERT_MSG(false, "MemoryRead16(%llx)", vaddr); }
    std::uint32_t MemoryRead32(u64 vaddr) override { ASSERT_MSG(false, "MemoryRead32(%llx)", vaddr); }
    std::uint64_t MemoryRead64(u64 vaddr) override { ASSERT_MSG(false, "MemoryRead64(%llx)", vaddr); }

    void MemoryWrite8(u64 vaddr, std::uint8_t value) override { ASSERT_MSG(false, "MemoryWrite8(%llx, %hhi)", vaddr, value); }
    void MemoryWrite16(u64 vaddr, std::uint16_t value) override { ASSERT_MSG(false, "MemoryWrite16(%llx, %hi)", vaddr, value); }
    void MemoryWrite32(u64 vaddr, std::uint32_t value) override { ASSERT_MSG(false, "MemoryWrite32(%llx, %i)", vaddr, value); }
    void MemoryWrite64(u64 vaddr, std::uint64_t value) override { ASSERT_MSG(false, "MemoryWrite64(%llx, %lli)", vaddr, value); }

    void InterpreterFallback(u64 pc, size_t num_instructions) override { ASSERT_MSG(false, "InterpreterFallback(%llx, %zu)", pc, num_instructions); }

    void CallSVC(std::uint32_t swi) override { ASSERT_MSG(false, "CallSVC(%u)", swi); }

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

TEST_CASE("A64", "[a64]") {
    TestEnv env;
    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&env}};
}