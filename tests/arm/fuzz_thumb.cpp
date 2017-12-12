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

#include <dynarmic/dynarmic.h>

#include "common/bit_util.h"
#include "common/common_types.h"
#include "frontend/disassembler/disassembler.h"
#include "frontend/ir/basic_block.h"
#include "frontend/translate/translate.h"
#include "ir_opt/passes.h"
#include "rand_int.h"
#include "skyeye_interpreter/dyncom/arm_dyncom_interpreter.h"
#include "skyeye_interpreter/skyeye_common/armstate.h"

struct WriteRecord {
    size_t size;
    u32 address;
    u64 data;
};

static bool operator==(const WriteRecord& a, const WriteRecord& b) {
    return std::tie(a.size, a.address, a.data) == std::tie(b.size, b.address, b.data);
}

static std::array<u16, 3000> code_mem{};
static std::vector<WriteRecord> write_records;

static bool IsReadOnlyMemory(u32 vaddr);
static u8 MemoryRead8(u32 vaddr);
static u16 MemoryRead16(u32 vaddr);
static u32 MemoryRead32(u32 vaddr);
static u64 MemoryRead64(u32 vaddr);
static void MemoryWrite8(u32 vaddr, u8 value);
static void MemoryWrite16(u32 vaddr, u16 value);
static void MemoryWrite32(u32 vaddr, u32 value);
static void MemoryWrite64(u32 vaddr, u64 value);
static void InterpreterFallback(u32 pc, Dynarmic::Jit* jit, void*);
static Dynarmic::UserCallbacks GetUserCallbacks();

static bool IsReadOnlyMemory(u32 vaddr) {
    return vaddr < code_mem.size();
}
static u8 MemoryRead8(u32 vaddr) {
    return static_cast<u8>(vaddr);
}
static u16 MemoryRead16(u32 vaddr) {
    return static_cast<u16>(vaddr);
}
static u32 MemoryRead32(u32 vaddr) {
    if (vaddr < code_mem.size() * sizeof(u16)) {
        size_t index = vaddr / sizeof(u16);
        return code_mem[index] | (code_mem[index+1] << 16);
    }
    return vaddr;
}
static u64 MemoryRead64(u32 vaddr) {
    return vaddr;
}
static u32 MemoryReadCode(u32 vaddr) {
    if (vaddr < code_mem.size() * sizeof(u16)) {
        size_t index = vaddr / sizeof(u16);
        return code_mem[index] | (code_mem[index + 1] << 16);
    }
    return 0xE7FEE7FE; // b +#0, b +#0
}

static void MemoryWrite8(u32 vaddr, u8 value){
    write_records.push_back({8, vaddr, value});
}
static void MemoryWrite16(u32 vaddr, u16 value){
    write_records.push_back({16, vaddr, value});
}
static void MemoryWrite32(u32 vaddr, u32 value){
    write_records.push_back({32, vaddr, value});
}
static void MemoryWrite64(u32 vaddr, u64 value){
    write_records.push_back({64, vaddr, value});
}

static void InterpreterFallback(u32 pc, Dynarmic::Jit* jit, void*) {
    ARMul_State interp_state{USER32MODE};
    interp_state.user_callbacks = GetUserCallbacks();
    interp_state.NumInstrsToExecute = 1;

    interp_state.Reg = jit->Regs();
    interp_state.Cpsr = jit->Cpsr();
    interp_state.Reg[15] = pc;

    InterpreterClearCache();
    InterpreterMainLoop(&interp_state);

    bool T = Dynarmic::Common::Bit<5>(interp_state.Cpsr);
    interp_state.Reg[15] &= T ? 0xFFFFFFFE : 0xFFFFFFFC;

    jit->Regs() = interp_state.Reg;
    jit->SetCpsr(interp_state.Cpsr);
}

static void Fail() {
    FAIL();
}

static void AddTicks(u64) {}

static Dynarmic::UserCallbacks GetUserCallbacks() {
    Dynarmic::UserCallbacks user_callbacks{};
    user_callbacks.InterpreterFallback = &InterpreterFallback;
    user_callbacks.CallSVC = (void (*)(u32)) &Fail;
    user_callbacks.memory.IsReadOnlyMemory = &IsReadOnlyMemory;
    user_callbacks.memory.Read8 = &MemoryRead8;
    user_callbacks.memory.Read16 = &MemoryRead16;
    user_callbacks.memory.Read32 = &MemoryRead32;
    user_callbacks.memory.Read64 = &MemoryRead64;
    user_callbacks.memory.ReadCode = &MemoryReadCode;
    user_callbacks.memory.Write8 = &MemoryWrite8;
    user_callbacks.memory.Write16 = &MemoryWrite16;
    user_callbacks.memory.Write32 = &MemoryWrite32;
    user_callbacks.memory.Write64 = &MemoryWrite64;
    user_callbacks.AddTicks = &AddTicks;
    return user_callbacks;
}

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

static bool DoesBehaviorMatch(const ARMul_State& interp, const Dynarmic::Jit& jit, const std::vector<WriteRecord>& interp_write_records, const std::vector<WriteRecord>& jit_write_records) {
    const auto interp_regs = interp.Reg;
    const auto jit_regs = jit.Regs();

    return std::equal(interp_regs.begin(), interp_regs.end(), jit_regs.begin(), jit_regs.end())
            && interp.Cpsr == jit.Cpsr()
            && interp_write_records == jit_write_records;
}

void FuzzJitThumb(const size_t instruction_count, const size_t instructions_to_execute_count, const size_t run_count, const std::function<u16()> instruction_generator) {
    // Prepare memory
    code_mem.fill(0xE7FE); // b +#0

    // Prepare test subjects
    ARMul_State interp{USER32MODE};
    interp.user_callbacks = GetUserCallbacks();
    Dynarmic::Jit jit{GetUserCallbacks()};

    for (size_t run_number = 0; run_number < run_count; run_number++) {
        interp.instruction_cache.clear();
        InterpreterClearCache();
        jit.ClearCache();

        // Setup initial state

        std::array<u32, 16> initial_regs;
        std::generate_n(initial_regs.begin(), 15, []{ return RandInt<u32>(0, 0xFFFFFFFF); });
        initial_regs[15] = 0;

        interp.Cpsr = 0x000001F0;
        interp.Reg = initial_regs;
        jit.SetCpsr(0x000001F0);
        jit.Regs() = initial_regs;

        std::generate_n(code_mem.begin(), instruction_count, instruction_generator);

        // Run interpreter
        write_records.clear();
        interp.NumInstrsToExecute = static_cast<unsigned>(instructions_to_execute_count);
        InterpreterMainLoop(&interp);
        auto interp_write_records = write_records;
        {
            bool T = Dynarmic::Common::Bit<5>(interp.Cpsr);
            interp.Reg[15] &= T ? 0xFFFFFFFE : 0xFFFFFFFC;
        }

        // Run jit
        write_records.clear();
        jit.Run(static_cast<unsigned>(instructions_to_execute_count));
        auto jit_write_records = write_records;

        // Compare
        if (!DoesBehaviorMatch(interp, jit, interp_write_records, jit_write_records)) {
            printf("Failed at execution number %zu\n", run_number);

            printf("\nInstruction Listing: \n");
            for (size_t i = 0; i < instruction_count; i++) {
                printf("%s\n", Dynarmic::Arm::DisassembleThumb16(code_mem[i]).c_str());
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
                printf("%zu [%x] = %" PRIu64 "\n", record.size, record.address, record.data);
            }

            printf("\nJIT Write Records:\n");
            for (auto& record : jit_write_records) {
                printf("%zu [%x] = %" PRIu64 "\n", record.size, record.address, record.data);
            }

            Dynarmic::Arm::PSR cpsr;
            cpsr.T(true);

            size_t num_insts = 0;
            while (num_insts < instructions_to_execute_count) {
                Dynarmic::IR::LocationDescriptor descriptor = {u32(num_insts * 4), cpsr, Dynarmic::Arm::FPSCR{}};
                Dynarmic::IR::Block ir_block = Dynarmic::Arm::Translate(descriptor, &MemoryReadCode);
                Dynarmic::Optimization::GetSetElimination(ir_block);
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

        if (run_number % 10 == 0) printf("%zu\r", run_number);
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
