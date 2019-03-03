/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <functional>
#include <tuple>

#include <catch.hpp>

#include <dynarmic/A32/a32.h>

#include "common/bit_util.h"
#include "common/common_types.h"
#include "frontend/A32/disassembler/disassembler.h"
#include "frontend/A32/FPSCR.h"
#include "frontend/A32/location_descriptor.h"
#include "frontend/A32/PSR.h"
#include "frontend/A32/translate/translate.h"
#include "frontend/ir/basic_block.h"
#include "ir_opt/passes.h"
#include "rand_int.h"
#include "testenv.h"
#include "A32/skyeye_interpreter/dyncom/arm_dyncom_interpreter.h"
#include "A32/skyeye_interpreter/skyeye_common/armstate.h"

static Dynarmic::A32::UserConfig GetUserConfig(ThumbTestEnv* testenv) {
    Dynarmic::A32::UserConfig user_config;
    user_config.enable_fast_dispatch = false;
    user_config.callbacks = testenv;
    return user_config;
}

using WriteRecords = std::map<u32, u8>;

struct ThumbInstGen final {
public:
    ThumbInstGen(const char* format, std::function<bool(u16)> is_valid = [](u16){ return true; }) : is_valid(is_valid) {
        REQUIRE(strlen(format) == 16);

        for (int i = 0; i < 16; i++) {
            const u16 bit = 1 << (15 - i);
            switch (format[i]) {
            case '0':
                mask |= bit;
                break;
            case '1':
                bits |= bit;
                mask |= bit;
                break;
            default:
                // Do nothing
                break;
            }
        }
    }
    u16 Generate() const {
        u16 inst;

        do {
            u16 random = RandInt<u16>(0, 0xFFFF);
            inst = bits | (random & ~mask);
        } while (!is_valid(inst));

        ASSERT((inst & mask) == bits);

        return inst;
    }
private:
    u16 bits = 0;
    u16 mask = 0;
    std::function<bool(u16)> is_valid;
};

static bool DoesBehaviorMatch(const ARMul_State& interp, const Dynarmic::A32::Jit& jit, WriteRecords& interp_write_records, WriteRecords& jit_write_records) {
    const auto interp_regs = interp.Reg;
    const auto jit_regs = jit.Regs();

    return std::equal(interp_regs.begin(), interp_regs.end(), jit_regs.begin(), jit_regs.end())
            && interp.Cpsr == jit.Cpsr()
            && interp_write_records == jit_write_records;
}

static void RunInstance(size_t run_number, ThumbTestEnv& test_env, ARMul_State& interp, Dynarmic::A32::Jit& jit, const ThumbTestEnv::RegisterArray& initial_regs,
                        size_t instruction_count, size_t instructions_to_execute_count) {
    interp.instruction_cache.clear();
    InterpreterClearCache();
    jit.ClearCache();

    // Setup initial state

    interp.Cpsr = 0x000001F0;
    interp.Reg = initial_regs;
    jit.SetCpsr(0x000001F0);
    jit.Regs() = initial_regs;

    // Run interpreter
    test_env.modified_memory.clear();
    interp.NumInstrsToExecute = static_cast<unsigned>(instructions_to_execute_count);
    InterpreterMainLoop(&interp);
    auto interp_write_records = test_env.modified_memory;
    {
        bool T = Dynarmic::Common::Bit<5>(interp.Cpsr);
        interp.Reg[15] &= T ? 0xFFFFFFFE : 0xFFFFFFFC;
    }

    // Run jit
    test_env.modified_memory.clear();
    test_env.ticks_left = instructions_to_execute_count;
    jit.Run();
    auto jit_write_records = test_env.modified_memory;

    // Compare
    if (!DoesBehaviorMatch(interp, jit, interp_write_records, jit_write_records)) {
        printf("Failed at execution number %zu\n", run_number);

        printf("\nInstruction Listing: \n");
        for (size_t i = 0; i < instruction_count; i++) {
            printf("%04x %s\n", test_env.code_mem[i], Dynarmic::A32::DisassembleThumb16(test_env.code_mem[i]).c_str());
        }

        printf("\nInitial Register Listing: \n");
        for (int i = 0; i <= 15; i++) {
            printf("%4i: %08x\n", i, initial_regs[i]);
        }

        printf("\nFinal Register Listing: \n");
        printf("      interp   jit\n");
        for (int i = 0; i <= 15; i++) {
            printf("%4i: %08x %08x %s\n", i, interp.Reg[i], jit.Regs()[i], interp.Reg[i] != jit.Regs()[i] ? "*" : "");
        }
        printf("CPSR: %08x %08x %s\n", interp.Cpsr, jit.Cpsr(), interp.Cpsr != jit.Cpsr() ? "*" : "");

        printf("\nInterp Write Records:\n");
        for (auto& record : interp_write_records) {
            printf("[%08x] = %02x\n", record.first, record.second);
        }

        printf("\nJIT Write Records:\n");
        for (auto& record : jit_write_records) {
            printf("[%08x] = %02x\n", record.first, record.second);
        }

        Dynarmic::A32::PSR cpsr;
        cpsr.T(true);

        size_t num_insts = 0;
        while (num_insts < instructions_to_execute_count) {
            Dynarmic::A32::LocationDescriptor descriptor = {u32(num_insts * 4), cpsr, Dynarmic::A32::FPSCR{}};
            Dynarmic::IR::Block ir_block = Dynarmic::A32::Translate(descriptor, [&test_env](u32 vaddr) { return test_env.MemoryReadCode(vaddr); }, {});
            Dynarmic::Optimization::A32GetSetElimination(ir_block);
            Dynarmic::Optimization::DeadCodeElimination(ir_block);
            Dynarmic::Optimization::A32ConstantMemoryReads(ir_block, &test_env);
            Dynarmic::Optimization::ConstantPropagation(ir_block);
            Dynarmic::Optimization::DeadCodeElimination(ir_block);
            Dynarmic::Optimization::VerificationPass(ir_block);
            printf("\n\nIR:\n%s", Dynarmic::IR::DumpBlock(ir_block).c_str());
            printf("\n\nx86_64:\n%s", jit.Disassemble(descriptor).c_str());
            num_insts += ir_block.CycleCount();
        }

#ifdef _MSC_VER
        __debugbreak();
#endif
        FAIL();
    }
}

void FuzzJitThumb(const size_t instruction_count, const size_t instructions_to_execute_count, const size_t run_count, const std::function<u16()> instruction_generator) {
    ThumbTestEnv test_env;

    // Prepare memory
    test_env.code_mem.resize(instruction_count + 1);
    test_env.code_mem.back() = 0xE7FE; // b +#0

    // Prepare test subjects
    ARMul_State interp{USER32MODE};
    interp.user_callbacks = &test_env;
    Dynarmic::A32::Jit jit{GetUserConfig(&test_env)};

    for (size_t run_number = 0; run_number < run_count; run_number++) {
        ThumbTestEnv::RegisterArray initial_regs;
        std::generate_n(initial_regs.begin(), 15, []{ return RandInt<u32>(0, 0xFFFFFFFF); });
        initial_regs[15] = 0;

        std::generate_n(test_env.code_mem.begin(), instruction_count, instruction_generator);

        RunInstance(run_number, test_env, interp, jit, initial_regs, instruction_count, instructions_to_execute_count);
    }
}

TEST_CASE("Fuzz Thumb instructions set 1", "[JitX64][Thumb]") {
    const std::array<ThumbInstGen, 25> instructions = {{
        ThumbInstGen("00000xxxxxxxxxxx"), // LSL <Rd>, <Rm>, #<imm5>
        ThumbInstGen("00001xxxxxxxxxxx"), // LSR <Rd>, <Rm>, #<imm5>
        ThumbInstGen("00010xxxxxxxxxxx"), // ASR <Rd>, <Rm>, #<imm5>
        ThumbInstGen("000110oxxxxxxxxx"), // ADD/SUB_reg
        ThumbInstGen("000111oxxxxxxxxx"), // ADD/SUB_imm
        ThumbInstGen("001ooxxxxxxxxxxx"), // ADD/SUB/CMP/MOV_imm
        ThumbInstGen("010000ooooxxxxxx"), // Data Processing
        ThumbInstGen("010001000hxxxxxx"), // ADD (high registers)
        ThumbInstGen("0100010101xxxxxx",  // CMP (high registers)
                     [](u16 inst){ return Dynarmic::Common::Bits<3, 5>(inst) != 0b111; }), // R15 is UNPREDICTABLE
        ThumbInstGen("0100010110xxxxxx",  // CMP (high registers)
                     [](u16 inst){ return Dynarmic::Common::Bits<0, 2>(inst) != 0b111; }), // R15 is UNPREDICTABLE
        ThumbInstGen("010001100hxxxxxx"), // MOV (high registers)
        ThumbInstGen("10110000oxxxxxxx"), // Adjust stack pointer
        ThumbInstGen("10110010ooxxxxxx"), // SXT/UXT
        ThumbInstGen("1011101000xxxxxx"), // REV
        ThumbInstGen("1011101001xxxxxx"), // REV16
        ThumbInstGen("1011101011xxxxxx"), // REVSH
        ThumbInstGen("01001xxxxxxxxxxx"), // LDR Rd, [PC, #]
        ThumbInstGen("0101oooxxxxxxxxx"), // LDR/STR Rd, [Rn, Rm]
        ThumbInstGen("011xxxxxxxxxxxxx"), // LDR(B)/STR(B) Rd, [Rn, #]
        ThumbInstGen("1000xxxxxxxxxxxx"), // LDRH/STRH Rd, [Rn, #offset]
        ThumbInstGen("1001xxxxxxxxxxxx"), // LDR/STR Rd, [SP, #]
        ThumbInstGen("1011010xxxxxxxxx",  // PUSH
                     [](u16 inst){ return Dynarmic::Common::Bits<0, 7>(inst) != 0; }), // Empty reg_list is UNPREDICTABLE
        ThumbInstGen("10111100xxxxxxxx",  // POP (P = 0)
                     [](u16 inst){ return Dynarmic::Common::Bits<0, 7>(inst) != 0; }), // Empty reg_list is UNPREDICTABLE
        ThumbInstGen("1100xxxxxxxxxxxx"), // STMIA/LDMIA
        ThumbInstGen("101101100101x000"), // SETEND
    }};

    auto instruction_select = [&]() -> u16 {
        size_t inst_index = RandInt<size_t>(0, instructions.size() - 1);

        return instructions[inst_index].Generate();
    };

    SECTION("single instructions") {
        FuzzJitThumb(1, 2, 10000, instruction_select);
    }

    SECTION("short blocks") {
        FuzzJitThumb(5, 6, 3000, instruction_select);
    }

    SECTION("long blocks") {
        FuzzJitThumb(1024, 1025, 1000, instruction_select);
    }
}

TEST_CASE("Fuzz Thumb instructions set 2 (affects PC)", "[JitX64][Thumb]") {
    const std::array<ThumbInstGen, 8> instructions = {{
        ThumbInstGen("01000111xmmmm000",  // BLX/BX
                     [](u16 inst){
                         u32 Rm = Dynarmic::Common::Bits<3, 6>(inst);
                         return Rm != 15;
                     }),
        ThumbInstGen("1010oxxxxxxxxxxx"), // add to pc/sp
        ThumbInstGen("11100xxxxxxxxxxx"), // B
        ThumbInstGen("01000100h0xxxxxx"), // ADD (high registers)
        ThumbInstGen("01000110h0xxxxxx"), // MOV (high registers)
        ThumbInstGen("1101ccccxxxxxxxx",  // B<cond>
                     [](u16 inst){
                         u32 c = Dynarmic::Common::Bits<9, 12>(inst);
                         return c < 0b1110; // Don't want SWI or undefined instructions.
                     }),
        ThumbInstGen("10110110011x0xxx"), // CPS
        ThumbInstGen("10111101xxxxxxxx"), // POP (R = 1)
    }};

    auto instruction_select = [&]() -> u16 {
        size_t inst_index = RandInt<size_t>(0, instructions.size() - 1);

        return instructions[inst_index].Generate();
    };

    FuzzJitThumb(1, 1, 10000, instruction_select);
}

TEST_CASE("Verify fix for off by one error in MemoryRead32 worked", "[Thumb]") {
    ThumbTestEnv test_env;

    // Prepare test subjects
    ARMul_State interp{USER32MODE};
    interp.user_callbacks = &test_env;
    Dynarmic::A32::Jit jit{GetUserConfig(&test_env)};

    constexpr ThumbTestEnv::RegisterArray initial_regs {
        0xe90ecd70,
        0x3e3b73c3,
        0x571616f9,
        0x0b1ef45a,
        0xb3a829f2,
        0x915a7a6a,
        0x579c38f4,
        0xd9ffe391,
        0x55b6682b,
        0x458d8f37,
        0x8f3eb3dc,
        0xe18c0e7d,
        0x6752657a,
        0x00001766,
        0xdbbf23e3,
        0x00000000,
    };

    test_env.code_mem = {
        0x40B8, // lsls r0, r7, #0
        0x01CA, // lsls r2, r1, #7
        0x83A1, // strh r1, [r4, #28]
        0x708A, // strb r2, [r1, #2]
        0xBCC4, // pop {r2, r6, r7}
        0xE7FE, // b +#0
    };

    RunInstance(1, test_env, interp, jit, initial_regs, 5, 5);
}
