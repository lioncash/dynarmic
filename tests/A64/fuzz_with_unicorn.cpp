/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <catch.hpp>

#include "common/llvm_disassemble.h"
#include "common/scope_exit.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/opcodes.h"
#include "inst_gen.h"
#include "rand_int.h"
#include "testenv.h"
#include "unicorn_emu/unicorn.h"

// Needs to be declaerd before <fmt/ostream.h>
static std::ostream& operator<<(std::ostream& o, const Dynarmic::A64::Vector& vec) {
    return o << fmt::format("{:016x}'{:016x}", vec[1], vec[0]);
}

#include <fmt/format.h>
#include <fmt/ostream.h>

using namespace Dynarmic;

static Vector RandomVector() {
    return {RandInt<u64>(0, ~u64(0)), RandInt<u64>(0, ~u64(0))};
}

static u32 GenRandomInst(u64 pc, bool is_last_inst) {
    static const std::vector<InstructionGenerator> instruction_generators = []{
        const std::vector<std::tuple<const char*, const char*>> list {
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "frontend/A64/decoder/a64.inc"
#undef INST
        };

        std::vector<InstructionGenerator> result;

        // List of instructions not to test
        const std::vector<std::string> do_not_test {
            // Unallocated encodings are invalid.
            "UnallocatedEncoding",
            // Unimplemented in QEMU
            "STLLR",
            // Unimplemented in QEMU
            "LDLAR",
            // Dynarmic and QEMU currently differ on how the exclusive monitor's address range works.
            "STXR", "STLXR", "STXP", "STLXP", "LDXR", "LDAXR", "LDXP", "LDAXP",
        };

        for (const auto& [fn, bitstring] : list) {
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                InstructionGenerator::AddInvalidInstruction(bitstring);
                continue;
            }
            result.emplace_back(InstructionGenerator{bitstring});
        }

        // Manually added exceptions:
        // FMOV_float_imm for half-precision floats (QEMU doesn't have half-precision support yet).
        InstructionGenerator::AddInvalidInstruction("00011110111iiiiiiii10000000ddddd");

        return result;
    }();

    const A64::LocationDescriptor location{pc, {}};

restart:
    const size_t index = RandInt<size_t>(0, instruction_generators.size() - 1);
    const u32 instruction = instruction_generators[index].Generate();

    IR::Block block{location};
    bool should_continue = A64::TranslateSingleInstruction(block, location, instruction);
    if (!should_continue && !is_last_inst)
        goto restart;
    if (auto terminal = block.GetTerminal(); boost::get<IR::Term::Interpret>(&terminal))
        goto restart;
    for (const auto& ir_inst : block)
        if (ir_inst.GetOpcode() == IR::Opcode::A64ExceptionRaised || ir_inst.GetOpcode() == IR::Opcode::A64CallSupervisor || ir_inst.GetOpcode() == IR::Opcode::A64DataCacheOperationRaised)
            goto restart;

    return instruction;
}

static void RunTestInstance(const std::array<u64, 31>& regs, const std::array<Vector, 32>& vecs, const size_t instructions_offset, const std::vector<u32>& instructions, const u32 pstate) {
    static TestEnv jit_env;
    static TestEnv uni_env;

    std::copy(instructions.begin(), instructions.end(), jit_env.code_mem.begin() + instructions_offset);
    std::copy(instructions.begin(), instructions.end(), uni_env.code_mem.begin() + instructions_offset);
    jit_env.code_mem[instructions.size() + instructions_offset] = 0x14000000; // B .
    uni_env.code_mem[instructions.size() + instructions_offset] = 0x14000000; // B .
    jit_env.modified_memory.clear();
    uni_env.modified_memory.clear();

    static Dynarmic::A64::Jit jit{Dynarmic::A64::UserConfig{&jit_env}};
    static Unicorn uni{uni_env};

    jit.SetRegisters(regs);
    jit.SetVectors(vecs);
    jit.SetPC(instructions_offset * 4);
    jit.SetSP(0x08000000);
    jit.SetPstate(pstate);
    jit.ClearCache();
    uni.SetRegisters(regs);
    uni.SetVectors(vecs);
    uni.SetPC(instructions_offset * 4);
    uni.SetSP(0x08000000);
    uni.SetPstate(pstate);
    uni.ClearPageCache();

    jit_env.ticks_left = instructions.size();
    jit.Run();

    uni_env.ticks_left = instructions.size();
    uni.Run();

    SCOPE_FAIL {
        fmt::print("Instruction Listing:\n");
        for (u32 instruction : instructions)
            fmt::print("{:08x} {}\n", instruction, Common::DisassembleAArch64(instruction));
        fmt::print("\n");

        fmt::print("Initial register listing:\n");
        for (size_t i = 0; i < regs.size(); ++i)
            fmt::print("{:3s}: {:016x}\n", static_cast<A64::Reg>(i), regs[i]);
        for (size_t i = 0; i < vecs.size(); ++i)
            fmt::print("{:3s}: {}\n", static_cast<A64::Vec>(i), vecs[i]);
        fmt::print("sp : 08000000\n");
        fmt::print("pc : {:016x}\n", instructions_offset * 4);
        fmt::print("p  : {:08x}\n", pstate);
        fmt::print("\n");

        fmt::print("Final register listing:\n");
        fmt::print("     unicorn          dynarmic\n");
        for (size_t i = 0; i < regs.size(); ++i)
            fmt::print("{:3s}: {:016x} {:016x} {}\n", static_cast<A64::Reg>(i), uni.GetRegisters()[i], jit.GetRegisters()[i], uni.GetRegisters()[i] != jit.GetRegisters()[i] ? "*" : "");
        for (size_t i = 0; i < vecs.size(); ++i)
            fmt::print("{:3s}: {} {} {}\n", static_cast<A64::Vec>(i), uni.GetVectors()[i], jit.GetVectors()[i], uni.GetVectors()[i] != jit.GetVectors()[i] ? "*" : "");
        fmt::print("sp : {:016x} {:016x} {}\n", uni.GetSP(), jit.GetSP(), uni.GetSP() != jit.GetSP() ? "*" : "");
        fmt::print("pc : {:016x} {:016x} {}\n", uni.GetPC(), jit.GetPC(), uni.GetPC() != jit.GetPC() ? "*" : "");
        fmt::print("p  : {:08x} {:08x} {}\n", uni.GetPstate(), jit.GetPstate(), (uni.GetPstate() & 0xF0000000) != (jit.GetPstate() & 0xF0000000) ? "*" : "");
        fmt::print("\n");

        fmt::print("Modified memory:\n");
        fmt::print("                 uni dyn\n");
        auto uni_iter = uni_env.modified_memory.begin();
        auto jit_iter = jit_env.modified_memory.begin();
        while (uni_iter != uni_env.modified_memory.end() || jit_iter != jit_env.modified_memory.end()) {
            if (uni_iter == uni_env.modified_memory.end() || (jit_iter != jit_env.modified_memory.end() && uni_iter->first > jit_iter->first)) {
                fmt::print("{:016x}:    {:02x} *\n", jit_iter->first, jit_iter->second);
                jit_iter++;
            } else if (jit_iter == jit_env.modified_memory.end() || jit_iter->first > uni_iter->first) {
                fmt::print("{:016x}: {:02x}    *\n", uni_iter->first, uni_iter->second);
                uni_iter++;
            } else if (uni_iter->first == jit_iter->first) {
                fmt::print("{:016x}: {:02x} {:02x} {}\n", uni_iter->first, uni_iter->second, jit_iter->second, uni_iter->second != jit_iter->second ? "*" : "");
                uni_iter++;
                jit_iter++;
            }
        }
        fmt::print("\n");

        fmt::print("x86_64:\n");
        fmt::print("{}\n", jit.Disassemble());
    };

    REQUIRE(uni.GetPC() == jit.GetPC());
    REQUIRE(uni.GetRegisters() == jit.GetRegisters());
    REQUIRE(uni.GetVectors() == jit.GetVectors());
    REQUIRE(uni.GetSP() == jit.GetSP());
    REQUIRE((uni.GetPstate() & 0xF0000000) == (jit.GetPstate() & 0xF0000000));
    REQUIRE(uni_env.modified_memory == jit_env.modified_memory);
}

TEST_CASE("A64: Single random instruction", "[a64]") {
    std::array<u64, 31> regs;
    std::array<Vector, 32> vecs;
    std::vector<u32> instructions(1);

    for (size_t iteration = 0; iteration < 100000; ++iteration) {
        std::generate(regs.begin(), regs.end(), []{ return RandInt<u64>(0, ~u64(0)); });
        std::generate(vecs.begin(), vecs.end(), RandomVector);
        instructions[0] = GenRandomInst(0, true);
        u32 pstate = RandInt<u32>(0, 0xF) << 28;

        INFO("Instruction: 0x" << std::hex << instructions[0]);

        RunTestInstance(regs, vecs, 100, instructions, pstate);
    }
}
