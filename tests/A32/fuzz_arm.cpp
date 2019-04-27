/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <tuple>
#include <vector>

#include <catch.hpp>
#include <dynarmic/A32/a32.h>

#include "common/common_types.h"
#include "common/fp/fpcr.h"
#include "common/fp/fpsr.h"
#include "common/scope_exit.h"
#include "frontend/A32/disassembler/disassembler.h"
#include "frontend/A32/location_descriptor.h"
#include "frontend/A32/translate/translate.h"
#include "frontend/A32/types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/location_descriptor.h"
#include "frontend/ir/opcodes.h"
#include "fuzz_util.h"
#include "rand_int.h"
#include "testenv.h"
#include "unicorn_emu/a32_unicorn.h"

// Must be declared last for all necessary operator<< to be declared prior to this.
#include <fmt/format.h>
#include <fmt/ostream.h>

namespace {
using namespace Dynarmic;

bool ShouldTestInst(u32 instruction, u32 pc, bool is_last_inst) {
    const A32::LocationDescriptor location{pc, {}, {}};
    IR::Block block{location};
    const bool should_continue = A32::TranslateSingleInstruction(block, location, instruction);

    if (!should_continue && !is_last_inst) {
        return false;
    }

    if (auto terminal = block.GetTerminal(); boost::get<IR::Term::Interpret>(&terminal)) {
        return false;
    }

    for (const auto& ir_inst : block) {
        switch (ir_inst.GetOpcode()) {
        case IR::Opcode::A32ExceptionRaised:
        case IR::Opcode::A32CallSupervisor:
        case IR::Opcode::A32CoprocInternalOperation:
        case IR::Opcode::A32CoprocSendOneWord:
        case IR::Opcode::A32CoprocSendTwoWords:
        case IR::Opcode::A32CoprocGetOneWord:
        case IR::Opcode::A32CoprocGetTwoWords:
        case IR::Opcode::A32CoprocLoadWords:
        case IR::Opcode::A32CoprocStoreWords:
            return false;
        default:
            continue;
        }
    }

    return true;
}

u32 GenRandomInst(u32 pc, bool is_last_inst) {
    static const struct InstructionGeneratorInfo {
        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;
    } instructions = []{
        const std::vector<std::tuple<std::string, const char*>> list {
#define INST(fn, name, bitstring) {#fn, bitstring},
#include "frontend/A32/decoder/arm.inc"
#include "frontend/A32/decoder/vfp2.inc"
#undef INST
        };

        std::vector<InstructionGenerator> generators;
        std::vector<InstructionGenerator> invalid;

        // List of instructions not to test
        static constexpr std::array do_not_test {
            // Translating load/stores
            "arm_LDRBT", "arm_LDRBT", "arm_LDRHT", "arm_LDRHT", "arm_LDRSBT", "arm_LDRSBT", "arm_LDRSHT", "arm_LDRSHT", "arm_LDRT", "arm_LDRT",
            "arm_STRBT", "arm_STRBT", "arm_STRHT", "arm_STRHT", "arm_STRT", "arm_STRT",
            // Exclusive load/stores
            "arm_LDREXB", "arm_LDREXD", "arm_LDREXH", "arm_LDREX",
            "arm_STREXB", "arm_STREXD", "arm_STREXH", "arm_STREX",
            "arm_SWP", "arm_SWPB",
            // Elevated load/store multiple instructions.
            "arm_LDM_eret", "arm_LDM_usr",
            "arm_STM_usr",
            // Hint instructions
            "arm_NOP", "arm_PLD", "arm_SEV", "arm_WFE", "arm_WFI", "arm_YIELD",
            // E, T, J
            "arm_BLX_reg", "arm_BLX_imm", "arm_BXJ", "arm_SETEND",
            // Coprocessor
            "arm_CDP", "arm_LDC", "arm_MCR", "arm_MCRR", "arm_MRC", "arm_MRRC", "arm_STC",
            // System
            "arm_CPS", "arm_RFE", "arm_SRS",
            // Undefined
            "arm_UDF",

            // Behavior differs from Qemu
            "arm_MSR_reg", "arm_MSR_imm", "arm_MRS",
        };

        for (const auto& [fn, bitstring] : list) {
            if (std::find(do_not_test.begin(), do_not_test.end(), fn) != do_not_test.end()) {
                invalid.emplace_back(InstructionGenerator{bitstring});
                continue;
            }
            generators.emplace_back(InstructionGenerator{bitstring});
        }
        return InstructionGeneratorInfo{generators, invalid};
    }();

    while (true) {
        const size_t index = RandInt<size_t>(0, instructions.generators.size() - 1);
        const u32 inst = instructions.generators[index].Generate();
        
        if (std::any_of(instructions.invalid.begin(), instructions.invalid.end(), [inst](const auto& invalid) { return invalid.Match(inst); })) {
            continue;
        }
        if (ShouldTestInst(inst, pc, is_last_inst)) {
            return inst;
        }
    }
}

Dynarmic::A32::UserConfig GetUserConfig(ArmTestEnv& testenv) {
    Dynarmic::A32::UserConfig user_config;
    user_config.enable_fast_dispatch = false;
    user_config.callbacks = &testenv;
    return user_config;
}

static void RunTestInstance(Dynarmic::A32::Jit& jit, A32Unicorn<ArmTestEnv>& uni,
                            ArmTestEnv& jit_env, ArmTestEnv& uni_env,
                            const A32Unicorn<ArmTestEnv>::RegisterArray& regs,
                            const A32Unicorn<ArmTestEnv>::ExtRegArray& vecs,
                            const std::vector<u32>& instructions, const u32 cpsr, const u32 fpscr) {
    const u32 initial_pc = regs[15];
    const u32 num_words = initial_pc / sizeof(u32);
    const u32 code_mem_size = num_words + instructions.size();

    jit_env.code_mem.resize(code_mem_size + 1);
    uni_env.code_mem.resize(code_mem_size + 1);

    std::copy(instructions.begin(), instructions.end(), jit_env.code_mem.begin() + num_words);
    std::copy(instructions.begin(), instructions.end(), uni_env.code_mem.begin() + num_words);
    jit_env.code_mem.back() = 0xEAFFFFFE; // B .
    uni_env.code_mem.back() = 0xEAFFFFFE; // B .
    jit_env.modified_memory.clear();
    uni_env.modified_memory.clear();
    jit_env.interrupts.clear();
    uni_env.interrupts.clear();

    jit.Regs() = regs;
    jit.ExtRegs() = vecs;
    jit.SetFpscr(fpscr);
    jit.SetCpsr(cpsr);
    jit.ClearCache();
    uni.SetRegisters(regs);
    uni.SetExtRegs(vecs);
    uni.SetFpscr(fpscr);
    uni.EnableFloatingPointAccess();
    uni.SetCpsr(cpsr);
    uni.ClearPageCache();

    jit_env.ticks_left = instructions.size();
    jit.Run();

    uni_env.ticks_left = instructions.size();
    uni.Run();

    SCOPE_FAIL {
        fmt::print("Instruction Listing:\n");
        for (u32 instruction : instructions) {
            fmt::print("{:08x} {}\n", instruction, A32::DisassembleArm(instruction));
        }
        fmt::print("\n");

        fmt::print("Initial register listing:\n");
        for (size_t i = 0; i < regs.size(); ++i) {
            fmt::print("{:3s}: {:08x}\n", static_cast<A32::Reg>(i), regs[i]);
        }
        for (size_t i = 0; i < vecs.size(); ++i) {
            fmt::print("{:3s}: {:08x}\n", static_cast<A32::ExtReg>(i), vecs[i]);
        }
        fmt::print("cpsr {:08x}\n", cpsr);
        fmt::print("fpcr {:08x}\n", fpscr);
        fmt::print("fpcr.AHP   {}\n", FP::FPCR{fpscr}.AHP());
        fmt::print("fpcr.DN    {}\n", FP::FPCR{fpscr}.DN());
        fmt::print("fpcr.FZ    {}\n", FP::FPCR{fpscr}.FZ());
        fmt::print("fpcr.RMode {}\n", static_cast<size_t>(FP::FPCR{fpscr}.RMode()));
        fmt::print("fpcr.FZ16  {}\n", FP::FPCR{fpscr}.FZ16());
        fmt::print("\n");

        fmt::print("Final register listing:\n");
        fmt::print("     unicorn  dynarmic\n");
        const auto uni_regs = uni.GetRegisters();
        for (size_t i = 0; i < regs.size(); ++i) {
            fmt::print("{:3s}: {:08x} {:08x} {}\n", static_cast<A32::Reg>(i), uni_regs[i], jit.Regs()[i], uni_regs[i] != jit.Regs()[i] ? "*" : "");
        }
        const auto uni_ext_regs = uni.GetExtRegs();
        for (size_t i = 0; i < vecs.size(); ++i) {
            fmt::print("s{:2d}: {:08x} {:08x} {}\n", static_cast<size_t>(i), uni_ext_regs[i], jit.ExtRegs()[i], uni_ext_regs[i] != jit.ExtRegs()[i] ? "*" : "");
        }
        fmt::print("cpsr {:08x} {:08x} {}\n", uni.GetCpsr(), jit.Cpsr(), uni.GetCpsr() != jit.Cpsr() ? "*" : "");
        fmt::print("fpsr {:08x} {:08x} {}\n", uni.GetFpscr(), jit.Fpscr(), (uni.GetFpscr() & 0xF0000000) != (jit.Fpscr() & 0xF0000000) ? "*" : "");
        fmt::print("\n");

        fmt::print("Modified memory:\n");
        fmt::print("                 uni dyn\n");
        auto uni_iter = uni_env.modified_memory.begin();
        auto jit_iter = jit_env.modified_memory.begin();
        while (uni_iter != uni_env.modified_memory.end() || jit_iter != jit_env.modified_memory.end()) {
            if (uni_iter == uni_env.modified_memory.end() || (jit_iter != jit_env.modified_memory.end() && uni_iter->first > jit_iter->first)) {
                fmt::print("{:08x}:    {:02x} *\n", jit_iter->first, jit_iter->second);
                jit_iter++;
            } else if (jit_iter == jit_env.modified_memory.end() || jit_iter->first > uni_iter->first) {
                fmt::print("{:08x}: {:02x}    *\n", uni_iter->first, uni_iter->second);
                uni_iter++;
            } else if (uni_iter->first == jit_iter->first) {
                fmt::print("{:08x}: {:02x} {:02x} {}\n", uni_iter->first, uni_iter->second, jit_iter->second, uni_iter->second != jit_iter->second ? "*" : "");
                uni_iter++;
                jit_iter++;
            }
        }
        fmt::print("\n");

        fmt::print("x86_64:\n");
        fmt::print("{}\n", jit.Disassemble(A32::LocationDescriptor{initial_pc, A32::PSR{cpsr}, A32::FPSCR{fpscr}}));

        fmt::print("Interrupts:\n");
        for (const auto& i : uni_env.interrupts) {
            std::puts(i.c_str());
        }
    };

    REQUIRE(uni_env.code_mem_modified_by_guest == jit_env.code_mem_modified_by_guest);
    if (uni_env.code_mem_modified_by_guest) {
        return;
    }

    // Qemu doesn't do Thumb transitions??
    {
        const u32 uni_pc = uni.GetPC();
        const bool is_thumb = (jit.Cpsr() & (1 << 5)) != 0;
        const u32 new_uni_pc = uni_pc & (is_thumb ? 0xFFFFFFFE : 0xFFFFFFFC);
        uni.SetPC(new_uni_pc);
    }

    REQUIRE(uni.GetRegisters() == jit.Regs());
    REQUIRE(uni.GetExtRegs() == jit.ExtRegs());
    REQUIRE((uni.GetCpsr() & ~(1 << 5)) == (jit.Cpsr() & ~(1 << 5)));
    REQUIRE((uni.GetFpscr() & 0xF0000000) == (jit.Fpscr() & 0xF0000000));
    REQUIRE(uni_env.modified_memory == jit_env.modified_memory);
    REQUIRE(uni_env.interrupts.empty());
}
} // Anonymous namespace

TEST_CASE("A32: Single random instruction", "[arm]") {
    ArmTestEnv jit_env{};
    ArmTestEnv uni_env{};

    Dynarmic::A32::Jit jit{GetUserConfig(jit_env)};
    A32Unicorn<ArmTestEnv> uni{uni_env};

    A32Unicorn<ArmTestEnv>::RegisterArray regs;
    A32Unicorn<ArmTestEnv>::ExtRegArray ext_reg;
    std::vector<u32> instructions(1);

    for (size_t iteration = 0; iteration < 100000; ++iteration) {
        std::generate(regs.begin(), regs.end(), [] { return RandInt<u32>(0, ~u32(0)); });
        std::generate(ext_reg.begin(), ext_reg.end(), [] { return RandInt<u32>(0, ~u32(0)); });

        instructions[0] = GenRandomInst(0, true);

        const u32 start_address = 100;
        const u32 cpsr = (RandInt<u32>(0, 0xF) << 28) | 0x13;
        const u32 fpcr = RandomFpcr();

        INFO("Instruction: 0x" << std::hex << instructions[0]);

        regs[15] = start_address;
        RunTestInstance(jit, uni, jit_env, uni_env, regs, ext_reg, instructions, cpsr, fpcr);
    }
}

TEST_CASE("A32: Small random block", "[arm]") {
    ArmTestEnv jit_env{};
    ArmTestEnv uni_env{};

    Dynarmic::A32::Jit jit{GetUserConfig(jit_env)};
    A32Unicorn<ArmTestEnv> uni{uni_env};

    A32Unicorn<ArmTestEnv>::RegisterArray regs;
    A32Unicorn<ArmTestEnv>::ExtRegArray ext_reg;
    std::vector<u32> instructions(5);

    for (size_t iteration = 0; iteration < 100000; ++iteration) {
        std::generate(regs.begin(), regs.end(), [] { return RandInt<u32>(0, ~u32(0)); });
        std::generate(ext_reg.begin(), ext_reg.end(), [] { return RandInt<u32>(0, ~u32(0)); });

        instructions[0] = GenRandomInst(0, false);
        instructions[1] = GenRandomInst(4, false);
        instructions[2] = GenRandomInst(8, false);
        instructions[3] = GenRandomInst(12, false);
        instructions[4] = GenRandomInst(16, true);

        const u32 start_address = 100;
        const u32 cpsr = (RandInt<u32>(0, 0xF) << 28) | 0x13;
        const u32 fpcr = RandomFpcr();

        INFO("Instruction 1: 0x" << std::hex << instructions[0]);
        INFO("Instruction 2: 0x" << std::hex << instructions[1]);
        INFO("Instruction 3: 0x" << std::hex << instructions[2]);
        INFO("Instruction 4: 0x" << std::hex << instructions[3]);
        INFO("Instruction 5: 0x" << std::hex << instructions[4]);

        regs[15] = start_address;
        RunTestInstance(jit, uni, jit_env, uni_env, regs, ext_reg, instructions, cpsr, fpcr);
    }
}
