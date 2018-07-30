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

#include "common/fp/fpsr.h"
#include "common/llvm_disassemble.h"
#include "common/scope_exit.h"
#include "frontend/A64/decoder/a64.h"
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

static bool ShouldTestInst(u32 instruction, u64 pc, bool is_last_inst) {
    const A64::LocationDescriptor location{pc, {}};
    IR::Block block{location};
    bool should_continue = A64::TranslateSingleInstruction(block, location, instruction);
    if (!should_continue && !is_last_inst)
        return false;
    if (auto terminal = block.GetTerminal(); boost::get<IR::Term::Interpret>(&terminal))
        return false;
    for (const auto& ir_inst : block) {
        switch (ir_inst.GetOpcode()) {
        case IR::Opcode::A64ExceptionRaised:
        case IR::Opcode::A64CallSupervisor:
        case IR::Opcode::A64DataCacheOperationRaised:
        case IR::Opcode::A64GetCNTPCT:
            return false;
        default:
            continue;
        }
    }
    return true;
}

static u32 GenRandomInst(u64 pc, bool is_last_inst) {
    static const std::vector<InstructionGenerator> instruction_generators = []{
        const std::vector<std::tuple<std::string, const char*>> list {
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "frontend/A64/decoder/a64.inc"
#undef INST
        };

        std::vector<InstructionGenerator> result;

        // List of instructions not to test
        const std::vector<std::string> do_not_test {
            // Unimplemented in QEMU
            "STLLR",
            // Unimplemented in QEMU
            "LDLAR",
            // Dynarmic and QEMU currently differ on how the exclusive monitor's address range works.
            "STXR", "STLXR", "STXP", "STLXP", "LDXR", "LDAXR", "LDXP", "LDAXP",
            // QEMU's implementation of FDIV is incorrect
            "FDIV_1", "FDIV_2",
        };

        for (const auto& [fn, bitstring] : list) {
            if (fn == "UnallocatedEncoding") {
                continue;
            }
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                InstructionGenerator::AddInvalidInstruction(bitstring);
                continue;
            }
            result.emplace_back(InstructionGenerator{bitstring});
        }
        return result;
    }();

    while (true) {
        const size_t index = RandInt<size_t>(0, instruction_generators.size() - 1);
        const u32 instruction = instruction_generators[index].Generate();

        if (ShouldTestInst(instruction, pc, is_last_inst)) {
            return instruction;
        }
    }
}

static u32 GenFloatInst(u64 pc, bool is_last_inst) {
    static const std::vector<InstructionGenerator> instruction_generators = []{
        const std::vector<std::tuple<std::string, std::string, const char*>> list {
#define INST(fn, name, bitstring) {#fn, #name, bitstring},
#include "frontend/A64/decoder/a64.inc"
#undef INST
        };

        // List of instructions not to test
        const std::vector<std::string> do_not_test {
            // QEMU's implementation of FCVT is incorrect
            "FCVT_float",
            // QEMU's implementation of FDIV is incorrect
            "FDIV_1", "FDIV_2",
        };

        std::vector<InstructionGenerator> result;

        for (const auto& [fn, name, bitstring] : list) {
            (void)name;

            if (fn[0] != 'F') {
                continue;
            } else if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                continue;
            }
            result.emplace_back(InstructionGenerator{bitstring});
        }

        return result;
    }();

    while (true) {
        const size_t index = RandInt<size_t>(0, instruction_generators.size() - 1);
        const u32 instruction = instruction_generators[index].Generate();

        if ((instruction & 0x00800000) == 0 && ShouldTestInst(instruction, pc, is_last_inst)) {
            return instruction;
        }
    }
}

static void RunTestInstance(const Unicorn::RegisterArray& regs, const Unicorn::VectorArray& vecs, const size_t instructions_offset,
                            const std::vector<u32>& instructions, const u32 pstate, const u32 fpcr) {
    static TestEnv jit_env{};
    static TestEnv uni_env{};

    std::copy(instructions.begin(), instructions.end(), jit_env.code_mem.begin() + instructions_offset);
    std::copy(instructions.begin(), instructions.end(), uni_env.code_mem.begin() + instructions_offset);
    jit_env.code_mem[instructions.size() + instructions_offset] = 0x14000000; // B .
    uni_env.code_mem[instructions.size() + instructions_offset] = 0x14000000; // B .
    jit_env.modified_memory.clear();
    uni_env.modified_memory.clear();
    jit_env.interrupts.clear();
    uni_env.interrupts.clear();

    Dynarmic::A64::UserConfig jit_user_config{&jit_env};
    // The below corresponds to the settings for qemu's aarch64_max_initfn
    jit_user_config.dczid_el0 = 7;
    jit_user_config.ctr_el0 = 0x80038003;

    static Dynarmic::A64::Jit jit{jit_user_config};
    static Unicorn uni{uni_env};

    jit.SetRegisters(regs);
    jit.SetVectors(vecs);
    jit.SetPC(instructions_offset * 4);
    jit.SetSP(0x08000000);
    jit.SetFpcr(fpcr);
    jit.SetFpsr(0);
    jit.SetPstate(pstate);
    jit.ClearCache();
    uni.SetRegisters(regs);
    uni.SetVectors(vecs);
    uni.SetPC(instructions_offset * 4);
    uni.SetSP(0x08000000);
    uni.SetFpcr(fpcr);
    uni.SetFpsr(0);
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
        fmt::print("fpcr {:08x}\n", fpcr);
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
        fmt::print("qc : {:08x} {:08x} {}\n", uni.GetFpsr(), jit.GetFpsr(), FP::FPSR{uni.GetFpsr()}.QC() != FP::FPSR{jit.GetFpsr()}.QC() ? "*" : "");
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

        fmt::print("Interrupts:\n");
        for (auto& i : uni_env.interrupts) {
            puts(i.c_str());
        }
    };

    REQUIRE(uni_env.code_mem_modified_by_guest == jit_env.code_mem_modified_by_guest);
    if (uni_env.code_mem_modified_by_guest) {
        return;
    }

    REQUIRE(uni.GetPC() == jit.GetPC());
    REQUIRE(uni.GetRegisters() == jit.GetRegisters());
    REQUIRE(uni.GetVectors() == jit.GetVectors());
    REQUIRE(uni.GetSP() == jit.GetSP());
    REQUIRE((uni.GetPstate() & 0xF0000000) == (jit.GetPstate() & 0xF0000000));
    REQUIRE(uni_env.modified_memory == jit_env.modified_memory);
    REQUIRE(uni_env.interrupts.empty());
    REQUIRE(FP::FPSR{uni.GetFpsr()}.QC() == FP::FPSR{jit.GetFpsr()}.QC());
}

TEST_CASE("A64: Single random instruction", "[a64]") {
    Unicorn::RegisterArray regs;
    Unicorn::VectorArray vecs;
    std::vector<u32> instructions(1);

    for (size_t iteration = 0; iteration < 100000; ++iteration) {
        std::generate(regs.begin(), regs.end(), []{ return RandInt<u64>(0, ~u64(0)); });
        std::generate(vecs.begin(), vecs.end(), RandomVector);
        instructions[0] = GenRandomInst(0, true);
        u32 pstate = RandInt<u32>(0, 0xF) << 28;
        u32 fpcr = (RandInt<u32>(0, 0x3) << 22) | (RandInt<u32>(0, 1) << 25); // randomize RMode and DN

        INFO("Instruction: 0x" << std::hex << instructions[0]);

        RunTestInstance(regs, vecs, 100, instructions, pstate, fpcr);
    }
}

TEST_CASE("A64: Floating point instructions", "[a64]") {
    static constexpr std::array<u64, 80> float_numbers {
        0x00000000, // positive zero
        0x00000001, // smallest positive denormal
        0x00000076, //
        0x00002b94, //
        0x00636d24, //
        0x007fffff, // largest positive denormal
        0x00800000, // smallest positive normalised real
        0x00800002, //
        0x01398437, //
        0x0ba98d27, //
        0x0ba98d7a, //
        0x751f853a, //
        0x7f7ffff0, //
        0x7f7fffff, // largest positive normalised real
        0x7f800000, // positive infinity
        0x7f800001, // first positive SNaN
        0x7f984a37, //
        0x7fbfffff, // last positive SNaN
        0x7fc00000, // first positive QNaN
        0x7fd9ba98, //
        0x7fffffff, // last positive QNaN
        0x80000000, // negative zero
        0x80000001, // smallest negative denormal
        0x80000076, //
        0x80002b94, //
        0x80636d24, //
        0x807fffff, // largest negative denormal
        0x80800000, // smallest negative normalised real
        0x80800002, //
        0x81398437, //
        0x8ba98d27, //
        0x8ba98d7a, //
        0xf51f853a, //
        0xff7ffff0, //
        0xff7fffff, // largest negative normalised real
        0xff800000, // negative infinity
        0xff800001, // first negative SNaN
        0xff984a37, //
        0xffbfffff, // last negative SNaN
        0xffc00000, // first negative QNaN
        0xffd9ba98, //
        0xffffffff, // last negative QNaN
        // some random numbers follow
        0x4f3495cb,
        0xe73a5134,
        0x7c994e9e,
        0x6164bd6c,
        0x09503366,
        0xbf5a97c9,
        0xe6ff1a14,
        0x77f31e2f,
        0xaab4d7d8,
        0x0966320b,
        0xb26bddee,
        0xb5c8e5d3,
        0x317285d3,
        0x3c9623b1,
        0x51fd2c7c,
        0x7b906a6c,
        0x3f800000,
        0x3dcccccd,
        0x3f000000,
        0x42280000,
        0x3eaaaaab,
        0xc1200000,
        0xbf800000,
        0xbf8147ae,
        0x3f8147ae,
        0x415df525,
        0xc79b271e,
        0x460e8c84,
        // some 64-bit-float upper-halves
        0x7ff00000, // +SNaN / +Inf
        0x7ff0abcd, // +SNaN
        0x7ff80000, // +QNaN
        0x7ff81234, // +QNaN
        0xfff00000, // -SNaN / -Inf
        0xfff05678, // -SNaN
        0xfff80000, // -QNaN
        0xfff809ef, // -QNaN
        0x3ff00000, // Number near +1.0
        0xbff00000, // Number near -1.0
    };

    const auto gen_float = [&]{
        return float_numbers[RandInt<size_t>(0, float_numbers.size() - 1)];
    };

    const auto gen_vector = [&]{
        u64 upper = (gen_float() << 32) | gen_float();
        u64 lower = (gen_float() << 32) | gen_float();
        return Vector{lower, upper};
    };

    Unicorn::RegisterArray regs;
    Unicorn::VectorArray vecs;
    std::vector<u32> instructions(1);

    for (size_t iteration = 0; iteration < 100000; ++iteration) {
        std::generate(regs.begin(), regs.end(), gen_float);
        std::generate(vecs.begin(), vecs.end(), gen_vector);
        instructions[0] = GenFloatInst(0, true);
        u32 pstate = RandInt<u32>(0, 0xF) << 28;
        u32 fpcr = (RandInt<u32>(0, 0x3) << 22) | (RandInt<u32>(0, 1) << 25); // randomize RMode and DN

        INFO("Instruction: 0x" << std::hex << instructions[0]);

        RunTestInstance(regs, vecs, 100, instructions, pstate, fpcr);
    }
}

TEST_CASE("A64: Small random block", "[a64]") {
    Unicorn::RegisterArray regs;
    Unicorn::VectorArray vecs;
    std::vector<u32> instructions(5);

    for (size_t iteration = 0; iteration < 100000; ++iteration) {
        std::generate(regs.begin(), regs.end(), [] { return RandInt<u64>(0, ~u64(0)); });
        std::generate(vecs.begin(), vecs.end(), RandomVector);

        instructions[0] = GenRandomInst(0, false);
        instructions[1] = GenRandomInst(4, false);
        instructions[2] = GenRandomInst(8, false);
        instructions[3] = GenRandomInst(12, false);
        instructions[4] = GenRandomInst(16, true);

        u32 pstate = RandInt<u32>(0, 0xF) << 28;
        u32 fpcr = (RandInt<u32>(0, 0x3) << 22) | (RandInt<u32>(0, 1) << 25); // randomize RMode and DN

        INFO("Instruction 1: 0x" << std::hex << instructions[0]);
        INFO("Instruction 2: 0x" << std::hex << instructions[1]);
        INFO("Instruction 3: 0x" << std::hex << instructions[2]);
        INFO("Instruction 4: 0x" << std::hex << instructions[3]);
        INFO("Instruction 5: 0x" << std::hex << instructions[4]);

        RunTestInstance(regs, vecs, 100, instructions, pstate, fpcr);
    }
}
