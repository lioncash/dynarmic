/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>

#include <catch.hpp>

#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"
#include "inst_gen.h"
#include "rand_int.h"
#include "testenv.h"
#include "unicorn_emu/unicorn.h"

using namespace Dynarmic;

static Vector RandomVector() {
    return {RandInt<u64>(0, ~u64(0)), RandInt<u64>(0, ~u64(0))};
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

static u32 GenRandomInst(u64 pc, bool is_last_inst) {
    const A64::LocationDescriptor location{pc, {}};

restart:
    const size_t index = RandInt<size_t>(0, instruction_generators.size() - 1);
    const u32 instruction = instruction_generators[index].Generate();

    IR::Block block{location};
    bool should_continue = A64::TranslateSingleInstruction(block, location, instruction);
    if (!should_continue && !is_last_inst)
        goto restart;
    for (const auto& ir_inst : block)
        if (ir_inst.GetOpcode() == IR::Opcode::A64ExceptionRaised || ir_inst.GetOpcode() == IR::Opcode::A64CallSupervisor)
            goto restart;

    return instruction;
}

static void RunTestInstance(const std::array<u64, 31>& regs, const std::array<Vector, 32>& vecs, const size_t instructions_offset, const std::vector<u32>& instructions, const u32 pstate) {
    TestEnv jit_env;
    TestEnv uni_env;

    std::copy(instructions.begin(), instructions.end(), jit_env.code_mem.begin() + instructions_offset);
    std::copy(instructions.begin(), instructions.end(), uni_env.code_mem.begin() + instructions_offset);
    jit_env.code_mem[instructions.size() + instructions_offset] = 0x14000000; // B .
    uni_env.code_mem[instructions.size() + instructions_offset] = 0x14000000; // B .

    Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&jit_env}};
    Unicorn uni{uni_env};

    jit.SetRegisters(regs);
    jit.SetVectors(vecs);
    jit.SetPC(instructions_offset * 4);
    jit.SetSP(0x8000000);
    jit.SetPstate(pstate);
    uni.SetRegisters(regs);
    uni.SetVectors(vecs);
    uni.SetPC(instructions_offset * 4);
    uni.SetSP(0x8000000);
    uni.SetPstate(pstate);

    jit_env.ticks_left = instructions.size();
    jit.Run();

    uni_env.ticks_left = instructions.size();
    uni.Run();

    REQUIRE(uni.GetPC() == jit.GetPC());
    REQUIRE(uni.GetRegisters() == jit.GetRegisters());
    REQUIRE(uni.GetVectors() == jit.GetVectors());
    REQUIRE(uni.GetSP() == jit.GetSP());
    REQUIRE((uni.GetPstate() & 0xF0000000) == (jit.GetPstate() & 0xF0000000));
    REQUIRE(uni_env.modified_memory == jit_env.modified_memory);
}

TEST_CASE("A64: Single random instruction", "[a64]") {
    for (size_t iteration = 0; iteration < 100000; ++iteration) {
        std::array<u64, 31> regs;
        std::generate_n(regs.begin(), 31, []{ return RandInt<u64>(0, ~u64(0)); });
        std::array<Vector, 32> vecs;
        std::generate_n(vecs.begin(), 32, []{ return RandomVector(); });
        std::vector<u32> instructions;
        instructions.push_back(GenRandomInst(0, true));
        u32 pstate = RandInt<u32>(0, 0xF) << 28;

        INFO("Instruction: 0x" << std::hex << instructions[0]);

        RunTestInstance(regs, vecs, 100, instructions, pstate);
    }
}
