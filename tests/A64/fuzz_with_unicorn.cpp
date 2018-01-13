/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>

#include <catch.hpp>
#include <unicorn/arm64.h>

#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"
#include "inst_gen.h"
#include "rand_int.h"
#include "testenv.h"
#include "unicorn_emu/unicorn.h"

using namespace Dynarmic;

TEST_CASE("A64: Unicorn sanity test", "[a64]") {
    TestEnv env;
    env.code_mem[0] = 0x8b020020; // ADD X0, X1, X2
    env.code_mem[1] = 0x14000000; // B .

    std::array<u64, 31> regs {
        0, 1, 2, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0
    };

    Unicorn unicorn{env};

    unicorn.SetRegisters(regs);
    unicorn.SetPC(0);

    env.ticks_left = 2;
    unicorn.Run();

    REQUIRE(unicorn.GetRegisters()[0] == 3);
    REQUIRE(unicorn.GetRegisters()[1] == 1);
    REQUIRE(unicorn.GetRegisters()[2] == 2);
    REQUIRE(unicorn.GetPC() == 4);
}

TEST_CASE("A64: Ensure 0xFFFF'FFFF'FFFF'FFFF is readable", "[a64]") {
    TestEnv env;

    env.code_mem[0] = 0x385fed99; // LDRB W25, [X12, #0xfffffffffffffffe]!
    env.code_mem[1] = 0x14000000; // B .

    std::array<u64, 31> regs{};
    regs[12] = 1;

    Unicorn unicorn{env};

    unicorn.SetRegisters(regs);
    unicorn.SetPC(0);

    env.ticks_left = 2;
    unicorn.Run();

    REQUIRE(unicorn.GetPC() == 4);
}

TEST_CASE("A64: Ensure is able to read across page boundaries", "[a64]") {
    TestEnv env;

    env.code_mem[0] = 0xb85f93d9; // LDUR W25, [X30, #0xfffffffffffffff9]
    env.code_mem[1] = 0x14000000; // B .

    std::array<u64, 31> regs{};
    regs[30] = 4;

    Unicorn unicorn{env};

    unicorn.SetRegisters(regs);
    unicorn.SetPC(0);

    env.ticks_left = 2;
    unicorn.Run();

    REQUIRE(unicorn.GetPC() == 4);
}

static std::vector<InstructionGenerator> instruction_generators = []{
    const std::vector<std::tuple<const char*, const char*>> list {
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "frontend/A64/decoder/a64.inc"
#undef INST
    };

    std::vector<InstructionGenerator> result;
    for (const auto& [fn, bitstring] : list) {
        if (std::strcmp(fn, "UnallocatedEncoding") == 0) {
            InstructionGenerator::AddInvalidInstruction(bitstring);
            continue;
        }
        result.emplace_back(InstructionGenerator{bitstring});
    }
    return result;
}();

static u32 GenRandomInst(u64 pc) {
    const A64::LocationDescriptor location{pc, {}};

restart:
    const size_t index = RandInt<size_t>(0, instruction_generators.size() - 1);
    const u32 instruction = instruction_generators[index].Generate();

    IR::Block block{location};
    bool should_continue = A64::TranslateSingleInstruction(block, location, instruction);
    if (!should_continue)
        goto restart;
    for (const auto& ir_inst : block)
        if (ir_inst.CausesCPUException() || ir_inst.IsMemoryWrite() || ir_inst.GetOpcode() == IR::Opcode::A64ExceptionRaised)
            goto restart;

    return instruction;
}

static void TestInstance(const std::array<u64, 31>& regs, const std::vector<u32>& instructions) {
    TestEnv jit_env;
    TestEnv uni_env;

    std::copy(instructions.begin(), instructions.end(), jit_env.code_mem.begin());
    std::copy(instructions.begin(), instructions.end(), uni_env.code_mem.begin());
    jit_env.code_mem[instructions.size()] = 0x14000000; // B .
    uni_env.code_mem[instructions.size()] = 0x14000000; // B .

    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&jit_env}};
    Unicorn uni{uni_env};

    jit.SetRegisters(regs);
    jit.SetPC(0);
    jit.SetSP(0x8000000);
    jit.SetPstate(0);
    uni.SetRegisters(regs);
    uni.SetPC(0);
    uni.SetSP(0x8000000);
    uni.SetPstate(0);

    jit_env.ticks_left = instructions.size();
    jit.Run();

    uni_env.ticks_left = instructions.size();
    uni.Run();

    REQUIRE(uni.GetRegisters() == jit.GetRegisters());
    REQUIRE(uni.GetPC() == jit.GetPC());
    REQUIRE(uni.GetSP() == jit.GetSP());
    REQUIRE((uni.GetPstate() & 0xF0000000) == (jit.GetPstate() & 0xF0000000));
}

TEST_CASE("A64: Single random instruction", "[a64]") {
    for (size_t iteration = 0; iteration < 1000000; ++iteration) {
        std::array<u64, 31> regs;
        std::generate_n(regs.begin(), 31, []{ return RandInt<u64>(0, ~u64(0)); });
        std::vector<u32> instructions;
        instructions.push_back(GenRandomInst(0));

        printf("%zu: %08x\n", iteration, instructions[0]);

        TestInstance(regs, instructions);
    }
}