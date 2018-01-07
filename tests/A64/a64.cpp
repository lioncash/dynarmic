/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include <catch.hpp>

#include <dynarmic/A64/a64.h>

#include "common/assert.h"
#include "common/common_types.h"

class TestEnv final : public Dynarmic::A64::UserCallbacks {
public:
    u64 ticks_left = 0;
    std::array<u32, 3000> code_mem{};

    std::uint32_t MemoryReadCode(u64 vaddr) override {
        if (vaddr < code_mem.size() * sizeof(u32)) {
            size_t index = vaddr / sizeof(u32);
            return code_mem[index];
        }
        return 0x14000000; // B .
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

TEST_CASE("A64: ADD", "[a64]") {
    TestEnv env;
    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&env}};

    env.code_mem[0] = 0x8b020020; // ADD X0, X1, X2
    env.code_mem[1] = 0x14000000; // B .

    jit.SetRegister(0, 0);
    jit.SetRegister(1, 1);
    jit.SetRegister(2, 2);
    jit.SetPC(0);

    env.ticks_left = 2;
    jit.Run();

    REQUIRE(jit.GetRegister(0) == 3);
    REQUIRE(jit.GetRegister(1) == 1);
    REQUIRE(jit.GetRegister(2) == 2);
    REQUIRE(jit.GetPC() == 4);
}

TEST_CASE("A64: AND", "[a64]") {
    TestEnv env;
    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&env}};

    env.code_mem[0] = 0x8a020020; // AND X0, X1, X2
    env.code_mem[1] = 0x14000000; // B .

    jit.SetRegister(0, 0);
    jit.SetRegister(1, 1);
    jit.SetRegister(2, 3);
    jit.SetPC(0);

    env.ticks_left = 2;
    jit.Run();

    REQUIRE(jit.GetRegister(0) == 1);
    REQUIRE(jit.GetRegister(1) == 1);
    REQUIRE(jit.GetRegister(2) == 3);
    REQUIRE(jit.GetPC() == 4);
}

TEST_CASE("A64: Bitmasks", "[a64]") {
    TestEnv env;
    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&env}};

    env.code_mem[0] = 0x3200c3e0; // ORR W0, WZR, #0x01010101
    env.code_mem[1] = 0x320c8fe1; // ORR W1, WZR, #0x00F000F0
    env.code_mem[2] = 0x320003e2; // ORR W2, WZR, #1
    env.code_mem[3] = 0x14000000; // B .

    jit.SetPC(0);

    env.ticks_left = 4;
    jit.Run();

    REQUIRE(jit.GetRegister(0) == 0x01010101);
    REQUIRE(jit.GetRegister(1) == 0x00F000F0);
    REQUIRE(jit.GetRegister(2) == 1);
    REQUIRE(jit.GetPC() == 12);
}

TEST_CASE("A64: ANDS NZCV", "[a64]") {
    TestEnv env;
    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&env}};

    env.code_mem[0] = 0x6a020020; // ANDS W0, W1, W2
    env.code_mem[1] = 0x14000000; // B .

    SECTION("N=1, Z=0") {
        jit.SetRegister(0, 0);
        jit.SetRegister(1, 0xFFFFFFFF);
        jit.SetRegister(2, 0xFFFFFFFF);
        jit.SetPC(0);

        env.ticks_left = 2;
        jit.Run();

        REQUIRE(jit.GetRegister(0) == 0xFFFFFFFF);
        REQUIRE(jit.GetRegister(1) == 0xFFFFFFFF);
        REQUIRE(jit.GetRegister(2) == 0xFFFFFFFF);
        REQUIRE(jit.GetPC() == 4);
        REQUIRE((jit.GetPstate() & 0xF0000000) == 0x80000000);
    }

    SECTION("N=0, Z=1") {
        jit.SetRegister(0, 0);
        jit.SetRegister(1, 0xFFFFFFFF);
        jit.SetRegister(2, 0x00000000);
        jit.SetPC(0);

        env.ticks_left = 2;
        jit.Run();

        REQUIRE(jit.GetRegister(0) == 0x00000000);
        REQUIRE(jit.GetRegister(1) == 0xFFFFFFFF);
        REQUIRE(jit.GetRegister(2) == 0x00000000);
        REQUIRE(jit.GetPC() == 4);
        REQUIRE((jit.GetPstate() & 0xF0000000) == 0x40000000);
    }
    SECTION("N=0, Z=0") {
        jit.SetRegister(0, 0);
        jit.SetRegister(1, 0x12345678);
        jit.SetRegister(2, 0x7324a993);
        jit.SetPC(0);

        env.ticks_left = 2;
        jit.Run();

        REQUIRE(jit.GetRegister(0) == 0x12240010);
        REQUIRE(jit.GetRegister(1) == 0x12345678);
        REQUIRE(jit.GetRegister(2) == 0x7324a993);
        REQUIRE(jit.GetPC() == 4);
        REQUIRE((jit.GetPstate() & 0xF0000000) == 0x00000000);
    }
}

TEST_CASE("A64: CBZ", "[a64]") {
    TestEnv env;
    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&env}};

    env.code_mem[0] = 0x34000060; // CBZ X0, label
    env.code_mem[1] = 0x320003e2; // MOV X2, 1
    env.code_mem[2] = 0x14000000; // B.
    env.code_mem[3] = 0x321f03e2; // label: MOV X2, 2
    env.code_mem[4] = 0x14000000; // B .

    SECTION("no branch") {
        jit.SetPC(0);
        jit.SetRegister(0, 1);

        env.ticks_left = 4;
        jit.Run();

        REQUIRE(jit.GetRegister(2) == 1);
        REQUIRE(jit.GetPC() == 8);
    }

    SECTION("branch") {
        jit.SetPC(0);
        jit.SetRegister(0, 0);

        env.ticks_left = 4;
        jit.Run();

        REQUIRE(jit.GetRegister(2) == 2);
        REQUIRE(jit.GetPC() == 16);
    }
}

TEST_CASE("A64: TBZ", "[a64]") {
    TestEnv env;
    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&env}};

    env.code_mem[0] = 0x36180060; // TBZ X0, 3, label
    env.code_mem[1] = 0x320003e2; // MOV X2, 1
    env.code_mem[2] = 0x14000000; // B .
    env.code_mem[3] = 0x321f03e2; // label: MOV X2, 2
    env.code_mem[4] = 0x14000000; // B .

    SECTION("no branch") {
        jit.SetPC(0);
        jit.SetRegister(0, 0xFF);

        env.ticks_left = 4;
        jit.Run();

        REQUIRE(jit.GetRegister(2) == 1);
        REQUIRE(jit.GetPC() == 8);
    }

    SECTION("branch with zero") {
        jit.SetPC(0);
        jit.SetRegister(0, 0);

        env.ticks_left = 4;
        jit.Run();

        REQUIRE(jit.GetRegister(2) == 2);
        REQUIRE(jit.GetPC() == 16);
    }

    SECTION("branch with non-zero") {
        jit.SetPC(0);
        jit.SetRegister(0, 1);

        env.ticks_left = 4;
        jit.Run();

        REQUIRE(jit.GetRegister(2) == 2);
        REQUIRE(jit.GetPC() == 16);
    }
}
