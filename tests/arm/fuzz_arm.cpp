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
#include <vector>

#include <catch.hpp>

#include <dynarmic/dynarmic.h>

#include "common/bit_util.h"
#include "common/common_types.h"
#include "frontend/disassembler/disassembler.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/location_descriptor.h"
#include "frontend/translate/translate.h"
#include "ir_opt/passes.h"
#include "rand_int.h"
#include "skyeye_interpreter/dyncom/arm_dyncom_interpreter.h"
#include "skyeye_interpreter/skyeye_common/armstate.h"

#ifdef __unix__
#include <signal.h>
#endif

using Dynarmic::Common::Bits;

struct WriteRecord {
    size_t size;
    u32 address;
    u64 data;
};

static bool operator==(const WriteRecord& a, const WriteRecord& b) {
    return std::tie(a.size, a.address, a.data) == std::tie(b.size, b.address, b.data);
}

static std::array<u32, 3000> code_mem{};
static std::vector<WriteRecord> write_records;

static bool IsReadOnlyMemory(u32 vaddr);
static u8 MemoryRead8(u32 vaddr);
static u16 MemoryRead16(u32 vaddr);
static u32 MemoryRead32(u32 vaddr);
static u64 MemoryRead64(u32 vaddr);
static u32 MemoryReadCode(u32 vaddr);
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
    return vaddr;
}
static u64 MemoryRead64(u32 vaddr) {
    return MemoryRead32(vaddr) | (u64(MemoryRead32(vaddr+4)) << 32);
}
static u32 MemoryReadCode(u32 vaddr) {
    if (vaddr < code_mem.size() * sizeof(u32)) {
        size_t index = vaddr / sizeof(u32);
        return code_mem[index];
    }
    return 0xeafffffe; // b +#0
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
    interp_state.ExtReg = jit->ExtRegs();
    interp_state.Cpsr = jit->Cpsr();
    interp_state.VFP[VFP_FPSCR] = jit->Fpscr();
    interp_state.Reg[15] = pc;

    InterpreterClearCache();
    InterpreterMainLoop(&interp_state);

    bool T = Dynarmic::Common::Bit<5>(interp_state.Cpsr);
    interp_state.Reg[15] &= T ? 0xFFFFFFFE : 0xFFFFFFFC;

    jit->Regs() = interp_state.Reg;
    jit->ExtRegs() = interp_state.ExtReg;
    jit->SetCpsr(interp_state.Cpsr);
    jit->SetFpscr(interp_state.VFP[VFP_FPSCR]);
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

struct InstructionGenerator final {
public:
    InstructionGenerator(const char* format, std::function<bool(u32)> is_valid = [](u32){ return true; }) : is_valid(is_valid) {
        REQUIRE(strlen(format) == 32);

        for (int i = 0; i < 32; i++) {
            const u32 bit = 1u << (31 - i);
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
    u32 Generate(bool condition = true) const {
        u32 inst;
        do {
            u32 random = RandInt<u32>(0, 0xFFFFFFFF);
            if (condition)
                random &= ~(0xF << 28);
            inst = bits | (random & ~mask);
        } while (!is_valid(inst));

        if (condition) {
            // Have a one-in-twenty-five chance of actually having a cond.
            if (RandInt(1, 25) == 1)
                inst |= RandInt(0x0, 0xD) << 28;
            else
                inst |= 0xE << 28;
        }

        return inst;
    }
    u32 Bits() const { return bits; }
    u32 Mask() const { return mask; }
    bool IsValid(u32 inst) const { return is_valid(inst); }
private:
    u32 bits = 0;
    u32 mask = 0;
    std::function<bool(u32)> is_valid;
};

static bool DoesBehaviorMatch(const ARMul_State& interp, const Dynarmic::Jit& jit, const std::vector<WriteRecord>& interp_write_records, const std::vector<WriteRecord>& jit_write_records) {
    return interp.Reg == jit.Regs()
           && interp.ExtReg == jit.ExtRegs()
           && interp.Cpsr == jit.Cpsr()
           //&& interp.VFP[VFP_FPSCR] == jit.Fpscr()
           && interp_write_records == jit_write_records;
}

void FuzzJitArm(const size_t instruction_count, const size_t instructions_to_execute_count, const size_t run_count, const std::function<u32()> instruction_generator) {
    // Prepare memory
    code_mem.fill(0xEAFFFFFE); // b +#0

    // Prepare test subjects
    ARMul_State interp{USER32MODE};
    interp.user_callbacks = GetUserCallbacks();
    Dynarmic::Jit jit{GetUserCallbacks()};

    for (size_t run_number = 0; run_number < run_count; run_number++) {
        interp.instruction_cache.clear();
        InterpreterClearCache();
        jit.ClearCache();

        // Setup initial state

        u32 initial_cpsr = 0x000001D0;

        std::array<u32, 16> initial_regs;
        std::generate_n(initial_regs.begin(), 15, []{ return RandInt<u32>(0, 0xFFFFFFFF); });
        initial_regs[15] = 0;

        std::array<u32, 64> initial_extregs;
        std::generate_n(initial_extregs.begin(), 64, []{ return RandInt<u32>(0, 0xFFFFFFFF); });

        u32 initial_fpscr = 0x01000000 | (RandInt<u32>(0, 3) << 22);

        interp.UnsetExclusiveMemoryAddress();
        interp.Cpsr = initial_cpsr;
        interp.Reg = initial_regs;
        interp.ExtReg = initial_extregs;
        interp.VFP[VFP_FPSCR] = initial_fpscr;
        jit.Reset();
        jit.SetCpsr(initial_cpsr);
        jit.Regs() = initial_regs;
        jit.ExtRegs() = initial_extregs;
        jit.SetFpscr(initial_fpscr);

        std::generate_n(code_mem.begin(), instruction_count, instruction_generator);

        // Run interpreter
        write_records.clear();
        std::vector<WriteRecord> interp_write_records;
        interp.NumInstrsToExecute = static_cast<unsigned>(instructions_to_execute_count);
        InterpreterMainLoop(&interp);
        interp_write_records = write_records;
        {
            bool T = Dynarmic::Common::Bit<5>(interp.Cpsr);
            interp.Reg[15] &= T ? 0xFFFFFFFE : 0xFFFFFFFC;
        }

        // Run jit
        write_records.clear();
        std::vector<WriteRecord> jit_write_records;
        try {
           jit.Run(static_cast<unsigned>(instructions_to_execute_count));
           jit_write_records = write_records;
        } catch (...) {
            printf("Caught something!\n");
            goto dump_state;
        }

        // Compare
        if (!DoesBehaviorMatch(interp, jit, interp_write_records, jit_write_records)) {
            printf("Failed at execution number %zu\n", run_number);

        dump_state:

            printf("\nInstruction Listing: \n");
            for (size_t i = 0; i < instruction_count; i++) {
                 printf("%x: %s\n", code_mem[i], Dynarmic::Arm::DisassembleArm(code_mem[i]).c_str());
            }

            printf("\nInitial Register Listing: \n");
            for (int i = 0; i <= 15; i++) {
                auto reg = Dynarmic::Arm::RegToString(static_cast<Dynarmic::Arm::Reg>(i));
                printf("%4s: %08x\n", reg, initial_regs[i]);
            }
            printf("CPSR: %08x\n", initial_cpsr);
            printf("FPSCR:%08x\n", initial_fpscr);
            for (int i = 0; i <= 63; i++) {
                printf("S%3i: %08x\n", i, initial_extregs[i]);
            }

            printf("\nFinal Register Listing: \n");
            printf("      interp   jit\n");
            for (int i = 0; i <= 15; i++) {
                auto reg = Dynarmic::Arm::RegToString(static_cast<Dynarmic::Arm::Reg>(i));
                printf("%4s: %08x %08x %s\n", reg, interp.Reg[i], jit.Regs()[i], interp.Reg[i] != jit.Regs()[i] ? "*" : "");
            }
            printf("CPSR: %08x %08x %s\n", interp.Cpsr, jit.Cpsr(), interp.Cpsr != jit.Cpsr() ? "*" : "");
            printf("FPSCR:%08x %08x %s\n", interp.VFP[VFP_FPSCR], jit.Fpscr(), interp.VFP[VFP_FPSCR] != jit.Fpscr() ? "*" : "");
            for (int i = 0; i <= 63; i++) {
                printf("S%3i: %08x %08x %s\n", i, interp.ExtReg[i], jit.ExtRegs()[i], interp.ExtReg[i] != jit.ExtRegs()[i] ? "*" : "");
            }

            printf("\nInterp Write Records:\n");
            for (auto& record : interp_write_records) {
                printf("%zu [%x] = %" PRIx64 "\n", record.size, record.address, record.data);
            }

            printf("\nJIT Write Records:\n");
            for (auto& record : jit_write_records) {
                printf("%zu [%x] = %" PRIx64 "\n", record.size, record.address, record.data);
            }

            size_t num_insts = 0;
            while (num_insts < instructions_to_execute_count) {
                Dynarmic::IR::LocationDescriptor descriptor = {u32(num_insts * 4), Dynarmic::Arm::PSR{}, Dynarmic::Arm::FPSCR{}};
                Dynarmic::IR::Block ir_block = Dynarmic::Arm::Translate(descriptor, &MemoryReadCode);
                Dynarmic::Optimization::GetSetElimination(ir_block);
                Dynarmic::Optimization::DeadCodeElimination(ir_block);
                Dynarmic::Optimization::VerificationPass(ir_block);
                printf("\n\nIR:\n%s", Dynarmic::IR::DumpBlock(ir_block).c_str());
                printf("\n\nx86_64:\n%s", jit.Disassemble(descriptor).c_str());
                num_insts += ir_block.CycleCount();
            }

            fflush(stdout);

#ifdef _MSC_VER
            __debugbreak();
#endif
#ifdef __unix__
            raise(SIGTRAP);
#endif
            FAIL();
        }

        if (run_number % 10 == 0) printf("%zu\r", run_number);
    }
}

TEST_CASE( "arm: Optimization Failure (Randomized test case)", "[arm]" ) {
    // This was a randomized test-case that was failing.
    //
    // IR produced for location {12, !T, !E} was:
    // %0     = GetRegister r1
    // %1     = SubWithCarry %0, #0x3e80000, #1
    // %2     = GetCarryFromOp %1
    // %3     = GetOverflowFromOp %1
    // %4     = MostSignificantBit %1
    //          SetNFlag %4
    // %6     = IsZero %1
    //          SetZFlag %6
    //          SetCFlag %2
    //          SetVFlag %3
    // %10    = GetRegister r5
    // %11    = AddWithCarry %10, #0x8a00, %2
    //          SetRegister r4, %11
    //
    // The reference to %2 in instruction %11 was the issue, because instruction %8
    // told the register allocator it was a Use but then modified the value.
    // Changing the EmitSet*Flag instruction to declare their arguments as UseScratch
    // solved this bug.

    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xe35f0cd9; // cmp pc, #55552
    code_mem[1] = 0xe11c0474; // tst r12, r4, ror r4
    code_mem[2] = 0xe1a006a7; // mov r0, r7, lsr #13
    code_mem[3] = 0xe35107fa; // cmp r1, #0x3E80000
    code_mem[4] = 0xe2a54c8a; // adc r4, r5, #35328
    code_mem[5] = 0xeafffffe; // b +#0

    jit.Regs() = {
            0x6973b6bb, 0x267ea626, 0x69debf49, 0x8f976895, 0x4ecd2d0d, 0xcf89b8c7, 0xb6713f85, 0x15e2aa5,
            0xcd14336a, 0xafca0f3e, 0xace2efd9, 0x68fb82cd, 0x775447c0, 0xc9e1f8cd, 0xebe0e626, 0x0
    };
    jit.SetCpsr(0x000001d0); // User-mode

    jit.Run(6);

    REQUIRE( jit.Regs()[0] == 0x00000af1 );
    REQUIRE( jit.Regs()[1] == 0x267ea626 );
    REQUIRE( jit.Regs()[2] == 0x69debf49 );
    REQUIRE( jit.Regs()[3] == 0x8f976895 );
    REQUIRE( jit.Regs()[4] == 0xcf8a42c8 );
    REQUIRE( jit.Regs()[5] == 0xcf89b8c7 );
    REQUIRE( jit.Regs()[6] == 0xb6713f85 );
    REQUIRE( jit.Regs()[7] == 0x015e2aa5 );
    REQUIRE( jit.Regs()[8] == 0xcd14336a );
    REQUIRE( jit.Regs()[9] == 0xafca0f3e );
    REQUIRE( jit.Regs()[10] == 0xace2efd9 );
    REQUIRE( jit.Regs()[11] == 0x68fb82cd );
    REQUIRE( jit.Regs()[12] == 0x775447c0 );
    REQUIRE( jit.Regs()[13] == 0xc9e1f8cd );
    REQUIRE( jit.Regs()[14] == 0xebe0e626 );
    REQUIRE( jit.Regs()[15] == 0x00000014 );
    REQUIRE( jit.Cpsr() == 0x200001d0 );
}

TEST_CASE( "arm: shsax r11, sp, r9 (Edge-case)", "[arm]" ) {
    // This was a randomized test-case that was failing.
    //
    // The issue here was one of the words to be subtracted was 0x8000.
    // When the 2s complement was calculated by (~a + 1), it was 0x8000.

    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xe63dbf59; // shsax r11, sp, r9
    code_mem[1] = 0xeafffffe; // b +#0

    jit.Regs() = {
            0x3a3b8b18, 0x96156555, 0xffef039f, 0xafb946f2, 0x2030a69a, 0xafe09b2a, 0x896823c8, 0xabde0ded,
            0x9825d6a6, 0x17498000, 0x999d2c95, 0x8b812a59, 0x209bdb58, 0x2f7fb1d4, 0x0f378107, 0x00000000
    };
    jit.SetCpsr(0x000001d0); // User-mode

    jit.Run(2);

    REQUIRE( jit.Regs()[0] == 0x3a3b8b18 );
    REQUIRE( jit.Regs()[1] == 0x96156555 );
    REQUIRE( jit.Regs()[2] == 0xffef039f );
    REQUIRE( jit.Regs()[3] == 0xafb946f2 );
    REQUIRE( jit.Regs()[4] == 0x2030a69a );
    REQUIRE( jit.Regs()[5] == 0xafe09b2a );
    REQUIRE( jit.Regs()[6] == 0x896823c8 );
    REQUIRE( jit.Regs()[7] == 0xabde0ded );
    REQUIRE( jit.Regs()[8] == 0x9825d6a6 );
    REQUIRE( jit.Regs()[9] == 0x17498000 );
    REQUIRE( jit.Regs()[10] == 0x999d2c95 );
    REQUIRE( jit.Regs()[11] == 0x57bfe48e );
    REQUIRE( jit.Regs()[12] == 0x209bdb58 );
    REQUIRE( jit.Regs()[13] == 0x2f7fb1d4 );
    REQUIRE( jit.Regs()[14] == 0x0f378107 );
    REQUIRE( jit.Regs()[15] == 0x00000004 );
    REQUIRE( jit.Cpsr() == 0x000001d0 );
}

TEST_CASE( "arm: uasx (Edge-case)", "[arm]" ) {
    // UASX's Rm<31:16> == 0x0000.
    // An implementation that depends on addition overflow to detect
    // if diff >= 0 will fail this testcase.

    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xe6549f35; // uasx r9, r4, r5
    code_mem[1] = 0xeafffffe; // b +#0

    jit.Regs()[4] = 0x8ed38f4c;
    jit.Regs()[5] = 0x0000261d;
    jit.Regs()[15] = 0x00000000;
    jit.SetCpsr(0x000001d0); // User-mode

    jit.Run(2);

    REQUIRE( jit.Regs()[4] == 0x8ed38f4c );
    REQUIRE( jit.Regs()[5] == 0x0000261d );
    REQUIRE( jit.Regs()[9] == 0xb4f08f4c );
    REQUIRE( jit.Regs()[15] == 0x00000004 );
    REQUIRE( jit.Cpsr() == 0x000301d0 );
}

struct VfpTest {
    u32 initial_fpscr;
    u32 a;
    u32 b;
    u32 result;
    u32 final_fpscr;
};

static void RunVfpTests(u32 instr, std::vector<VfpTest> tests) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = instr;
    code_mem[1] = 0xeafffffe; // b +#0

    printf("vfp test 0x%08x\r", instr);

    for (const auto& test : tests) {
        jit.Regs()[15] = 0;
        jit.SetCpsr(0x000001d0);
        jit.ExtRegs()[4] = test.a;
        jit.ExtRegs()[6] = test.b;
        jit.SetFpscr(test.initial_fpscr);

        jit.Run(2);

        const auto check = [&test, &jit](bool p) {
            if (!p) {
                printf("Failed test:\n");
                printf("initial_fpscr: 0x%08x\n", test.initial_fpscr);
                printf("a:             0x%08x (jit: 0x%08x)\n", test.a, jit.ExtRegs()[4]);
                printf("b:             0x%08x (jit: 0x%08x)\n", test.b, jit.ExtRegs()[6]);
                printf("result:        0x%08x (jit: 0x%08x)\n", test.result, jit.ExtRegs()[2]);
                printf("final_fpscr:   0x%08x (jit: 0x%08x)\n", test.final_fpscr, jit.Fpscr());
                FAIL();
            }
        };

        REQUIRE( jit.Regs()[15] == 4 );
        REQUIRE( jit.Cpsr() == 0x000001d0 );
        check( jit.ExtRegs()[2] == test.result );
        check( jit.ExtRegs()[4] == test.a );
        check( jit.ExtRegs()[6] == test.b );
        check( jit.Fpscr() == test.final_fpscr );
    }
}

TEST_CASE("vfp: vadd", "[vfp]") {
    // vadd.f32 s2, s4, s6
    RunVfpTests(0xEE321A03, {
#include "vfp_vadd_f32.inc"
    });
}

TEST_CASE("vfp: vsub", "[vfp]") {
    // vsub.f32 s2, s4, s6
    RunVfpTests(0xEE321A43, {
#include "vfp_vsub_f32.inc"
    });
}

TEST_CASE("VFP: VMOV", "[JitX64][vfp]") {
    const auto is_valid = [](u32 instr) -> bool {
        return Bits<0, 6>(instr) != 0b111111
                && Bits<12, 15>(instr) != 0b1111
                && Bits<16, 19>(instr) != 0b1111
                && Bits<12, 15>(instr) != Bits<16, 19>(instr);
    };

    const std::array<InstructionGenerator, 8> instructions = {{
        InstructionGenerator("cccc11100000ddddtttt1011D0010000", is_valid),
        InstructionGenerator("cccc11100001nnnntttt1011N0010000", is_valid),
        InstructionGenerator("cccc11100000nnnntttt1010N0010000", is_valid),
        InstructionGenerator("cccc11100001nnnntttt1010N0010000", is_valid),
        InstructionGenerator("cccc11000100uuuutttt101000M1mmmm", is_valid),
        InstructionGenerator("cccc11000101uuuutttt101000M1mmmm", is_valid),
        InstructionGenerator("cccc11000100uuuutttt101100M1mmmm", is_valid),
        InstructionGenerator("cccc11000101uuuutttt101100M1mmmm", is_valid),
    }};

    FuzzJitArm(1, 1, 10000, [&instructions]() -> u32 {
        return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
    });
}

TEST_CASE("VFP: VMOV (reg), VLDR, VSTR", "[JitX64][vfp]") {
    const std::array<InstructionGenerator, 4> instructions = {{
        InstructionGenerator("1111000100000001000000e000000000"), // SETEND
        InstructionGenerator("cccc11101D110000dddd101z01M0mmmm"), // VMOV (reg)
        InstructionGenerator("cccc1101UD01nnnndddd101zvvvvvvvv"), // VLDR
        InstructionGenerator("cccc1101UD00nnnndddd101zvvvvvvvv"), // VSTR
    }};

    FuzzJitArm(5, 6, 10000, [&instructions]() -> u32 {
        return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
    });
}

TEST_CASE("VFP: VCMP", "[JitX64][vfp]") {
    const std::array<InstructionGenerator, 2> instructions = {{
        InstructionGenerator("cccc11101D110100dddd101zE1M0mmmm"), // VCMP
        InstructionGenerator("cccc11101D110101dddd101zE1000000"), // VCMP (zero)
    }};

    FuzzJitArm(5, 6, 10000, [&instructions]() -> u32 {
        return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
    });
}

TEST_CASE("Fuzz ARM data processing instructions", "[JitX64]") {
    const std::array<InstructionGenerator, 16> imm_instructions = {{
        InstructionGenerator("cccc0010101Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0010100Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0010000Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0011110Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc00110111nnnn0000rrrrvvvvvvvv"),
        InstructionGenerator("cccc00110101nnnn0000rrrrvvvvvvvv"),
        InstructionGenerator("cccc0010001Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0011101S0000ddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0011111S0000ddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0011100Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0010011Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0010111Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0010110Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc0010010Snnnnddddrrrrvvvvvvvv"),
        InstructionGenerator("cccc00110011nnnn0000rrrrvvvvvvvv"),
        InstructionGenerator("cccc00110001nnnn0000rrrrvvvvvvvv"),
    }};

    const std::array<InstructionGenerator, 16> reg_instructions = {{
        InstructionGenerator("cccc0000101Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0000100Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0000000Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0001110Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc00010111nnnn0000vvvvvrr0mmmm"),
        InstructionGenerator("cccc00010101nnnn0000vvvvvrr0mmmm"),
        InstructionGenerator("cccc0000001Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0001101S0000ddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0001111S0000ddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0001100Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0000011Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0000111Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0000110Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc0000010Snnnnddddvvvvvrr0mmmm"),
        InstructionGenerator("cccc00010011nnnn0000vvvvvrr0mmmm"),
        InstructionGenerator("cccc00010001nnnn0000vvvvvrr0mmmm"),
    }};

    const std::array<InstructionGenerator, 16> rsr_instructions = {{
        InstructionGenerator("cccc0000101Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0000100Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0000000Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0001110Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc00010111nnnn0000ssss0rr1mmmm"),
        InstructionGenerator("cccc00010101nnnn0000ssss0rr1mmmm"),
        InstructionGenerator("cccc0000001Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0001101S0000ddddssss0rr1mmmm"),
        InstructionGenerator("cccc0001111S0000ddddssss0rr1mmmm"),
        InstructionGenerator("cccc0001100Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0000011Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0000111Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0000110Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc0000010Snnnnddddssss0rr1mmmm"),
        InstructionGenerator("cccc00010011nnnn0000ssss0rr1mmmm"),
        InstructionGenerator("cccc00010001nnnn0000ssss0rr1mmmm"),
    }};

    auto instruction_select = [&](bool Rd_can_be_r15) -> auto {
        return [&, Rd_can_be_r15]() -> u32 {
            size_t instruction_set = RandInt<size_t>(0, 2);

            u32 cond = 0xE;
            // Have a one-in-twenty-five chance of actually having a cond.
            if (RandInt(1, 25) == 1) {
                cond = RandInt<u32>(0x0, 0xD);
            }

            u32 S = RandInt<u32>(0, 1);

            switch (instruction_set) {
            case 0: {
                InstructionGenerator instruction = imm_instructions[RandInt<size_t>(0, imm_instructions.size() - 1)];
                u32 Rd = RandInt<u32>(0, Rd_can_be_r15 ? 15 : 14);
                if (Rd == 15) S = false;
                u32 Rn = RandInt<u32>(0, 15);
                u32 shifter_operand = RandInt<u32>(0, 0xFFF);
                u32 assemble_randoms = (shifter_operand << 0) | (Rd << 12) | (Rn << 16) | (S << 20) | (cond << 28);
                return instruction.Bits() | (assemble_randoms & ~instruction.Mask());
            }
            case 1: {
                InstructionGenerator instruction = reg_instructions[RandInt<size_t>(0, reg_instructions.size() - 1)];
                u32 Rd = RandInt<u32>(0, Rd_can_be_r15 ? 15 : 14);
                if (Rd == 15) S = false;
                u32 Rn = RandInt<u32>(0, 15);
                u32 shifter_operand = RandInt<u32>(0, 0xFFF);
                u32 assemble_randoms =
                        (shifter_operand << 0) | (Rd << 12) | (Rn << 16) | (S << 20) | (cond << 28);
                return instruction.Bits() | (assemble_randoms & ~instruction.Mask());
            }
            case 2: {
                InstructionGenerator instruction = rsr_instructions[RandInt<size_t>(0, rsr_instructions.size() - 1)];
                u32 Rd = RandInt<u32>(0, 14); // Rd can never be 15.
                u32 Rn = RandInt<u32>(0, 14);
                u32 Rs = RandInt<u32>(0, 14);
                int rotate = RandInt<int>(0, 3);
                u32 Rm = RandInt<u32>(0, 14);
                u32 assemble_randoms =
                        (Rm << 0) | (rotate << 5) | (Rs << 8) | (Rd << 12) | (Rn << 16) | (S << 20) | (cond << 28);
                return instruction.Bits() | (assemble_randoms & ~instruction.Mask());
            }
            }
            return 0;
        };
    };

    SECTION("single instructions") {
        FuzzJitArm(1, 2, 10000, instruction_select(/*Rd_can_be_r15=*/false));
    }

    SECTION("short blocks") {
        FuzzJitArm(5, 6, 10000, instruction_select(/*Rd_can_be_r15=*/false));
    }

    SECTION("long blocks") {
        FuzzJitArm(1024, 1025, 200, instruction_select(/*Rd_can_be_r15=*/false));
    }

    SECTION("R15") {
        FuzzJitArm(1, 1, 10000, instruction_select(/*Rd_can_be_r15=*/true));
    }
}

TEST_CASE("Fuzz ARM load/store instructions (byte, half-word, word)", "[JitX64]") {
    auto EXD_valid = [](u32 inst) -> bool {
        return Bits<0, 3>(inst) % 2 == 0 && Bits<0, 3>(inst) != 14 && Bits<12, 15>(inst) != (Bits<0, 3>(inst) + 1);
    };

    auto STREX_valid = [](u32 inst) -> bool {
        return Bits<12, 15>(inst) != Bits<16, 19>(inst) && Bits<12, 15>(inst) != Bits<0, 3>(inst);
    };

    auto SWP_valid = [](u32 inst) -> bool {
        return Bits<12, 15>(inst) != Bits<16, 19>(inst) && Bits<16, 19>(inst) != Bits<0, 3>(inst);
    };

    auto LDREXD_valid = [](u32 inst) -> bool {
        return Bits<12, 15>(inst) != 14;
    };

    auto D_valid = [](u32 inst) -> bool {
        u32 Rn = Bits<16, 19>(inst);
        u32 Rd = Bits<12, 15>(inst);
        u32 Rm = Bits<0, 3>(inst);
        return Rn % 2 == 0 && Rd % 2 == 0 && Rm != Rd && Rm != Rd + 1 && Rd != 14;
    };

    const std::array<InstructionGenerator, 32> instructions = {{
        InstructionGenerator("cccc010pu0w1nnnnddddvvvvvvvvvvvv"), // LDR_imm
        InstructionGenerator("cccc011pu0w1nnnnddddvvvvvrr0mmmm"), // LDR_reg
        InstructionGenerator("cccc010pu1w1nnnnddddvvvvvvvvvvvv"), // LDRB_imm
        InstructionGenerator("cccc011pu1w1nnnnddddvvvvvrr0mmmm"), // LDRB_reg
        InstructionGenerator("cccc000pu1w0nnnnddddvvvv1101vvvv", D_valid), // LDRD_imm
        InstructionGenerator("cccc000pu0w0nnnndddd00001101mmmm", D_valid), // LDRD_reg
        InstructionGenerator("cccc010pu0w0nnnnddddvvvvvvvvvvvv"), // STR_imm
        InstructionGenerator("cccc011pu0w0nnnnddddvvvvvrr0mmmm"), // STR_reg
        InstructionGenerator("cccc010pu1w0nnnnddddvvvvvvvvvvvv"), // STRB_imm
        InstructionGenerator("cccc011pu1w0nnnnddddvvvvvrr0mmmm"), // STRB_reg
        InstructionGenerator("cccc000pu1w0nnnnddddvvvv1111vvvv", D_valid), // STRD_imm
        InstructionGenerator("cccc000pu0w0nnnndddd00001111mmmm", D_valid), // STRD_reg
        InstructionGenerator("cccc000pu1w1nnnnddddvvvv1011vvvv"), // LDRH_imm
        InstructionGenerator("cccc000pu0w1nnnndddd00001011mmmm"), // LDRH_reg
        InstructionGenerator("cccc000pu1w1nnnnddddvvvv1101vvvv"), // LDRSB_imm
        InstructionGenerator("cccc000pu0w1nnnndddd00001101mmmm"), // LDRSB_reg
        InstructionGenerator("cccc000pu1w1nnnnddddvvvv1111vvvv"), // LDRSH_imm
        InstructionGenerator("cccc000pu0w1nnnndddd00001111mmmm"), // LDRSH_reg
        InstructionGenerator("cccc000pu1w0nnnnddddvvvv1011vvvv"), // STRH_imm
        InstructionGenerator("cccc000pu0w0nnnndddd00001011mmmm"), // STRH_reg
        InstructionGenerator("1111000100000001000000e000000000"), // SETEND
        InstructionGenerator("11110101011111111111000000011111"), // CLREX
        InstructionGenerator("cccc00011001nnnndddd111110011111"), // LDREX
        InstructionGenerator("cccc00011101nnnndddd111110011111"), // LDREXB
        InstructionGenerator("cccc00011011nnnndddd111110011111", LDREXD_valid), // LDREXD
        InstructionGenerator("cccc00011111nnnndddd111110011111"), // LDREXH
        InstructionGenerator("cccc00011000nnnndddd11111001mmmm", STREX_valid), // STREX
        InstructionGenerator("cccc00011100nnnndddd11111001mmmm", STREX_valid), // STREXB
        InstructionGenerator("cccc00011010nnnndddd11111001mmmm",
                             [=](u32 inst) { return EXD_valid(inst) && STREX_valid(inst); }), // STREXD
        InstructionGenerator("cccc00011110nnnndddd11111001mmmm", STREX_valid), // STREXH
        InstructionGenerator("cccc00010000nnnntttt00001001uuuu", SWP_valid), // SWP
        InstructionGenerator("cccc00010100nnnntttt00001001uuuu", SWP_valid), // SWPB
    }};

    auto instruction_select = [&]() -> u32 {
        size_t inst_index = RandInt<size_t>(0, instructions.size() - 1);

        while (true) {
            u32 cond = 0xE;
            // Have a one-in-twenty-five chance of actually having a cond.
            if (RandInt(1, 25) == 1) {
                cond = RandInt<u32>(0x0, 0xD);
            }

            u32 Rn = RandInt<u32>(0, 14);
            u32 Rd = RandInt<u32>(0, 14);
            u32 W = 0;
            u32 P = RandInt<u32>(0, 1);
            if (P) W = RandInt<u32>(0, 1);
            u32 U = RandInt<u32>(0, 1);
            u32 rand = RandInt<u32>(0, 0xFF);
            u32 Rm = RandInt<u32>(0, 14);

            if (!P || W) {
                while (Rn == Rd) {
                    Rn = RandInt<u32>(0, 14);
                    Rd = RandInt<u32>(0, 14);
                }
            }

            u32 assemble_randoms = (Rm << 0) | (rand << 4) | (Rd << 12) | (Rn << 16) | (W << 21) | (U << 23) | (P << 24) | (cond << 28);
            u32 inst = instructions[inst_index].Bits() | (assemble_randoms & (~instructions[inst_index].Mask()));
            if (instructions[inst_index].IsValid(inst)) {
                return inst;
            }
        }
    };

    SECTION("short blocks") {
        FuzzJitArm(5, 6, 30000, instruction_select);
    }
}

TEST_CASE("Fuzz ARM load/store multiple instructions", "[JitX64]") {
    const std::array<InstructionGenerator, 2> instructions = {{
        InstructionGenerator("cccc100pu0w1nnnnxxxxxxxxxxxxxxxx"), // LDM
        InstructionGenerator("cccc100pu0w0nnnnxxxxxxxxxxxxxxxx"), // STM
    }};

    auto instruction_select = [&]() -> u32 {
        size_t inst_index = RandInt<size_t>(0, instructions.size() - 1);

        u32 cond = 0xE;
        // Have a one-in-twenty-five chance of actually having a cond.
        if (RandInt(1, 25) == 1) {
            cond = RandInt<u32>(0x0, 0xD);
        }

        u32 reg_list = RandInt<u32>(1, 0xFFFF);
        u32 Rn = RandInt<u32>(0, 14);
        u32 flags = RandInt<u32>(0, 0xF);

        while (true) {
            if (inst_index == 1 && (flags & 2)) {
                if (reg_list & (1 << Rn))
                    reg_list &= ~((1 << Rn) - 1);
            } else if (inst_index == 0 && (flags & 2)) {
                reg_list &= ~(1 << Rn);
            }

            if (reg_list)
                break;

            reg_list = RandInt<u32>(1, 0xFFFF);
        }

        u32 assemble_randoms = (reg_list << 0) | (Rn << 16) | (flags << 24) | (cond << 28);

        return instructions[inst_index].Bits() | (assemble_randoms & (~instructions[inst_index].Mask()));
    };

    FuzzJitArm(1, 1, 10000, instruction_select);
}

TEST_CASE("Fuzz ARM branch instructions", "[JitX64]") {
    const std::array<InstructionGenerator, 6> instructions = {{
        InstructionGenerator("1111101hvvvvvvvvvvvvvvvvvvvvvvvv"),
        InstructionGenerator("cccc000100101111111111110011mmmm",
                             [](u32 instr) { return Bits<0, 3>(instr) != 0b1111; }), // R15 is UNPREDICTABLE
        InstructionGenerator("cccc1010vvvvvvvvvvvvvvvvvvvvvvvv"),
        InstructionGenerator("cccc1011vvvvvvvvvvvvvvvvvvvvvvvv"),
        InstructionGenerator("cccc000100101111111111110001mmmm"),
        InstructionGenerator("cccc000100101111111111110010mmmm"),
    }};
    FuzzJitArm(1, 1, 10000, [&instructions]() -> u32 {
        return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
    });
}

TEST_CASE("Fuzz ARM reversal instructions", "[JitX64]") {
    const auto is_valid = [](u32 instr) -> bool {
        // R15 is UNPREDICTABLE
        return Bits<0, 3>(instr) != 0b1111 && Bits<12, 15>(instr) != 0b1111;
    };

    const std::array<InstructionGenerator, 3> rev_instructions = {{
        InstructionGenerator("cccc011010111111dddd11110011mmmm", is_valid),
        InstructionGenerator("cccc011010111111dddd11111011mmmm", is_valid),
        InstructionGenerator("cccc011011111111dddd11111011mmmm", is_valid),
    }};

    SECTION("Reverse tests") {
        FuzzJitArm(1, 1, 10000, [&rev_instructions]() -> u32 {
            return rev_instructions[RandInt<size_t>(0, rev_instructions.size() - 1)].Generate();
        });
    }
}

TEST_CASE("Fuzz ARM extension instructions", "[JitX64]") {
    const auto is_valid = [](u32 instr) -> bool {
        // R15 as Rd or Rm is UNPREDICTABLE
        return Bits<0, 3>(instr) != 0b1111 && Bits<12, 15>(instr) != 0b1111;
    };

    const std::array<InstructionGenerator, 6> signed_instructions = {{
        InstructionGenerator("cccc011010101111ddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc011010001111ddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc011010111111ddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc01101010nnnnddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc01101000nnnnddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc01101011nnnnddddrr000111mmmm", is_valid),
    }};

    const std::array<InstructionGenerator, 6> unsigned_instructions = {{
        InstructionGenerator("cccc011011101111ddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc011011001111ddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc011011111111ddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc01101110nnnnddddrr000111mmmm", is_valid),
        InstructionGenerator("cccc01101100nnnnddddrr000111mmmm", is_valid), //UXTAB16
        InstructionGenerator("cccc01101111nnnnddddrr000111mmmm", is_valid),
    }};

    SECTION("Signed extension") {
        FuzzJitArm(1, 1, 10000, [&signed_instructions]() -> u32 {
            return signed_instructions[RandInt<size_t>(0, signed_instructions.size() - 1)].Generate();
        });
    }

    SECTION("Unsigned extension") {
        FuzzJitArm(1, 1, 10000, [&unsigned_instructions]() -> u32 {
            return unsigned_instructions[RandInt<size_t>(0, unsigned_instructions.size() - 1)].Generate();
        });
    }
}

TEST_CASE("Fuzz ARM multiply instructions", "[JitX64]") {
    auto validate_d_m_n = [](u32 inst) -> bool {
        return Bits<16, 19>(inst) != 15 &&
               Bits<8, 11>(inst) != 15 &&
               Bits<0, 3>(inst) != 15;
    };
    auto validate_d_a_m_n = [&](u32 inst) -> bool {
        return validate_d_m_n(inst) &&
               Bits<12, 15>(inst) != 15;
    };
    auto validate_h_l_m_n = [&](u32 inst) -> bool {
        return validate_d_a_m_n(inst) &&
               Bits<12, 15>(inst) != Bits<16, 19>(inst);
    };

    const std::array<InstructionGenerator, 21> instructions = {{
        InstructionGenerator("cccc0000001Sddddaaaammmm1001nnnn", validate_d_a_m_n), // MLA
        InstructionGenerator("cccc0000000Sdddd0000mmmm1001nnnn", validate_d_m_n),   // MUL

        InstructionGenerator("cccc0000111Sddddaaaammmm1001nnnn", validate_h_l_m_n), // SMLAL
        InstructionGenerator("cccc0000110Sddddaaaammmm1001nnnn", validate_h_l_m_n), // SMULL
        InstructionGenerator("cccc00000100ddddaaaammmm1001nnnn", validate_h_l_m_n), // UMAAL
        InstructionGenerator("cccc0000101Sddddaaaammmm1001nnnn", validate_h_l_m_n), // UMLAL
        InstructionGenerator("cccc0000100Sddddaaaammmm1001nnnn", validate_h_l_m_n), // UMULL

        InstructionGenerator("cccc00010100ddddaaaammmm1xy0nnnn", validate_h_l_m_n), // SMLALxy
        InstructionGenerator("cccc00010000ddddaaaammmm1xy0nnnn", validate_d_a_m_n), // SMLAxy
        InstructionGenerator("cccc00010110dddd0000mmmm1xy0nnnn", validate_d_m_n),   // SMULxy

        InstructionGenerator("cccc00010010ddddaaaammmm1y00nnnn", validate_d_a_m_n), // SMLAWy
        InstructionGenerator("cccc00010010dddd0000mmmm1y10nnnn", validate_d_m_n),   // SMULWy

        InstructionGenerator("cccc01110101dddd1111mmmm00R1nnnn", validate_d_m_n),   // SMMUL
        InstructionGenerator("cccc01110101ddddaaaammmm00R1nnnn", validate_d_a_m_n), // SMMLA
        InstructionGenerator("cccc01110101ddddaaaammmm11R1nnnn", validate_d_a_m_n), // SMMLS
        InstructionGenerator("cccc01110000ddddaaaammmm00M1nnnn", validate_d_a_m_n), // SMLAD
        InstructionGenerator("cccc01110100ddddaaaammmm00M1nnnn", validate_h_l_m_n), // SMLALD
        InstructionGenerator("cccc01110000ddddaaaammmm01M1nnnn", validate_d_a_m_n), // SMLSD
        InstructionGenerator("cccc01110100ddddaaaammmm01M1nnnn", validate_h_l_m_n), // SMLSLD
        InstructionGenerator("cccc01110000dddd1111mmmm00M1nnnn", validate_d_m_n),   // SMUAD
        InstructionGenerator("cccc01110000dddd1111mmmm01M1nnnn", validate_d_m_n),   // SMUSD
    }};

    SECTION("Multiply") {
        FuzzJitArm(1, 1, 10000, [&]() -> u32 {
            return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
        });
    }
}

TEST_CASE("Fuzz ARM parallel instructions", "[JitX64][parallel]") {
    const auto is_valid = [](u32 instr) -> bool {
        // R15 as Rd, Rn, or Rm is UNPREDICTABLE
        return Bits<0, 3>(instr) != 0b1111 && Bits<12, 15>(instr) != 0b1111 && Bits<16, 19>(instr) != 0b1111;
    };

    const auto is_sel_valid = [](u32 instr) -> bool {
        // R15 as Rd, Rn, or Rm is UNPREDICTABLE
        return Bits<0, 3>(instr) != 0b1111 && Bits<12, 15>(instr) != 0b1111 && Bits<16, 19>(instr) != 0b1111;
    };

    const auto is_msr_valid = [](u32 instr) -> bool {
        // Mask can not be 0
        return Bits<18, 19>(instr) != 0b00;
    };

    const InstructionGenerator cpsr_setter = InstructionGenerator("11100011001001001111rrrrvvvvvvvv", is_msr_valid); // MSR_Imm write GE
    const InstructionGenerator sel_instr = InstructionGenerator("111001101000nnnndddd11111011mmmm", is_sel_valid); // SEL

    const std::array<InstructionGenerator, 4> modulo_add_instructions = {{
        InstructionGenerator("cccc01100001nnnndddd11111001mmmm", is_valid), // SADD8
        InstructionGenerator("cccc01100001nnnndddd11110001mmmm", is_valid), // SADD16
        InstructionGenerator("cccc01100101nnnndddd11111001mmmm", is_valid), // UADD8
        InstructionGenerator("cccc01100101nnnndddd11110001mmmm", is_valid), // UADD16
    }};

    const std::array<InstructionGenerator, 4> modulo_sub_instructions = {{
        InstructionGenerator("cccc01100001nnnndddd11111111mmmm", is_valid), // SSUB8
        InstructionGenerator("cccc01100001nnnndddd11110111mmmm", is_valid), // SSUB16
        InstructionGenerator("cccc01100101nnnndddd11111111mmmm", is_valid), // USUB8
        InstructionGenerator("cccc01100101nnnndddd11110111mmmm", is_valid), // USUB16
    }};

    const std::array<InstructionGenerator, 4> modulo_exchange_instructions = {{
        InstructionGenerator("cccc01100001nnnndddd11110011mmmm", is_valid), // SASX
        InstructionGenerator("cccc01100001nnnndddd11110101mmmm", is_valid), // SSAX
        InstructionGenerator("cccc01100101nnnndddd11110011mmmm", is_valid), // UASX
        InstructionGenerator("cccc01100101nnnndddd11110101mmmm", is_valid), // USAX
    }};

    const std::array<InstructionGenerator, 12> saturating_instructions = {{
        InstructionGenerator("cccc01100010nnnndddd11111001mmmm", is_valid), // QADD8
        InstructionGenerator("cccc01100010nnnndddd11111111mmmm", is_valid), // QSUB8
        InstructionGenerator("cccc01100110nnnndddd11111001mmmm", is_valid), // UQADD8
        InstructionGenerator("cccc01100110nnnndddd11111111mmmm", is_valid), // UQSUB8
        InstructionGenerator("cccc01100010nnnndddd11110001mmmm", is_valid), // QADD16
        InstructionGenerator("cccc01100010nnnndddd11110111mmmm", is_valid), // QSUB16
        InstructionGenerator("cccc01100110nnnndddd11110001mmmm", is_valid), // UQADD16
        InstructionGenerator("cccc01100110nnnndddd11110111mmmm", is_valid), // UQSUB16
        InstructionGenerator("cccc01100010nnnndddd11110011mmmm", is_valid), // QASX
        InstructionGenerator("cccc01100010nnnndddd11110101mmmm", is_valid), // QSAX
        InstructionGenerator("cccc01100110nnnndddd11110011mmmm", is_valid), // UQASX
        InstructionGenerator("cccc01100110nnnndddd11110101mmmm", is_valid), // UQSAX
    }};

    const std::array<InstructionGenerator, 12> halving_instructions = {{
        InstructionGenerator("cccc01100011nnnndddd11111001mmmm", is_valid), // SHADD8
        InstructionGenerator("cccc01100011nnnndddd11110001mmmm", is_valid), // SHADD16
        InstructionGenerator("cccc01100011nnnndddd11110011mmmm", is_valid), // SHASX
        InstructionGenerator("cccc01100011nnnndddd11110101mmmm", is_valid), // SHSAX
        InstructionGenerator("cccc01100011nnnndddd11111111mmmm", is_valid), // SHSUB8
        InstructionGenerator("cccc01100011nnnndddd11110111mmmm", is_valid), // SHSUB16
        InstructionGenerator("cccc01100111nnnndddd11111001mmmm", is_valid), // UHADD8
        InstructionGenerator("cccc01100111nnnndddd11110001mmmm", is_valid), // UHADD16
        InstructionGenerator("cccc01100111nnnndddd11110011mmmm", is_valid), // UHASX
        InstructionGenerator("cccc01100111nnnndddd11110101mmmm", is_valid), // UHSAX
        InstructionGenerator("cccc01100111nnnndddd11111111mmmm", is_valid), // UHSUB8
        InstructionGenerator("cccc01100111nnnndddd11110111mmmm", is_valid), // UHSUB16
    }};

    size_t index = 0;
    const auto also_test_sel = [&](u32 inst) -> u32 {
        switch (index++ % 3) {
        case 1:
            return cpsr_setter.Generate(false);
        case 2:
            return sel_instr.Generate(false);
        }
        return inst;
    };

    SECTION("Parallel Add (Modulo)") {
        FuzzJitArm(4, 5, 10000, [&]() -> u32 {
            return also_test_sel(modulo_add_instructions[RandInt<size_t>(0, modulo_add_instructions.size() - 1)].Generate());
        });
    }

    SECTION("Parallel Subtract (Modulo)") {
        FuzzJitArm(4, 5, 10000, [&]() -> u32 {
            return also_test_sel(modulo_sub_instructions[RandInt<size_t>(0, modulo_sub_instructions.size() - 1)].Generate());
        });
    }

    SECTION("Parallel Exchange (Modulo)") {
        FuzzJitArm(4, 5, 10000, [&]() -> u32 {
            return also_test_sel(modulo_exchange_instructions[RandInt<size_t>(0, modulo_exchange_instructions.size() - 1)].Generate());
        });
    }

    SECTION("Parallel Add/Subtract (Saturating)") {
        FuzzJitArm(4, 5, 10000, [&]() -> u32 {
            return also_test_sel(saturating_instructions[RandInt<size_t>(0, saturating_instructions.size() - 1)].Generate());
        });
    }

    SECTION("Parallel Add/Subtract (Halving)") {
        FuzzJitArm(4, 5, 10000, [&]() -> u32 {
            return also_test_sel(halving_instructions[RandInt<size_t>(0, halving_instructions.size() - 1)].Generate());
        });
    }

    SECTION("Fuzz SEL") {
        // Alternate between a SEL and a MSR to change the CPSR, thus changing the expected result of the next SEL
        bool set_cpsr = true;
        FuzzJitArm(5, 6, 10000, [&sel_instr, &cpsr_setter, &set_cpsr]() -> u32 {
            set_cpsr ^= true;
            if (set_cpsr)
                return cpsr_setter.Generate(false);
            return sel_instr.Generate(false);
        });
    }
}

TEST_CASE("Fuzz ARM sum of absolute differences", "[JitX64]") {
    auto validate_d_m_n = [](u32 inst) -> bool {
        return Bits<16, 19>(inst) != 15 &&
               Bits<8, 11>(inst) != 15 &&
               Bits<0, 3>(inst) != 15;
    };
    auto validate_d_a_m_n = [&](u32 inst) -> bool {
        return validate_d_m_n(inst) &&
               Bits<12, 15>(inst) != 15;
    };

    const std::array<InstructionGenerator, 2> differences_instructions = {{
        InstructionGenerator("cccc01111000dddd1111mmmm0001nnnn", validate_d_m_n), // USAD8
        InstructionGenerator("cccc01111000ddddaaaammmm0001nnnn", validate_d_a_m_n), // USADA8
    }};

    SECTION("Sum of Absolute Differences (Differences)") {
        FuzzJitArm(1, 1, 10000, [&differences_instructions]() -> u32 {
            return differences_instructions[RandInt<size_t>(0, differences_instructions.size() - 1)].Generate();
        });
    }
}

TEST_CASE( "SMUAD", "[JitX64]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xE700F211; // smuad r0, r1, r2

    jit.Regs() = {
            0, // Rd
            0x80008000, // Rn
            0x80008000, // Rm
            0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
    };
    jit.SetCpsr(0x000001d0); // User-mode

    jit.Run(6);

    REQUIRE(jit.Regs()[0] == 0x80000000);
    REQUIRE(jit.Regs()[1] == 0x80008000);
    REQUIRE(jit.Regs()[2] == 0x80008000);
    REQUIRE(jit.Cpsr() == 0x080001d0);
}

TEST_CASE("VFP: VPUSH, VPOP", "[JitX64][vfp]") {
    const auto is_valid = [](u32 instr) -> bool {
        auto regs = (instr & 0x100) ? (Bits<0, 7>(instr) >> 1) : Bits<0, 7>(instr);
        auto base = Bits<12, 15>(instr);
        unsigned d;
        if (instr & 0x100) {
            d = (base + ((instr & 0x400000) ? 16 : 0));
        } else {
            d = ((base << 1) + ((instr & 0x400000) ? 1 : 0));
        }
        // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE
        return regs != 0 && regs <= 16 && (d + regs) <= 32;
    };

    const std::array<InstructionGenerator, 2> instructions = {{
        InstructionGenerator("cccc11010D101101dddd101zvvvvvvvv", is_valid), // VPUSH
        InstructionGenerator("cccc11001D111101dddd1010vvvvvvvv", is_valid), // VPOP
    }};

    FuzzJitArm(5, 6, 10000, [&instructions]() -> u32 {
        return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
    });
}

TEST_CASE("Test ARM misc instructions", "[JitX64]") {
    const auto is_clz_valid = [](u32 instr) -> bool {
        // R15 as Rd, or Rm is UNPREDICTABLE
        return Bits<0, 3>(instr) != 0b1111 && Bits<12, 15>(instr) != 0b1111;
    };

    const InstructionGenerator clz_instr = InstructionGenerator("cccc000101101111dddd11110001mmmm", is_clz_valid); // CLZ

    SECTION("Fuzz CLZ") {
        FuzzJitArm(1, 1, 1000, [&clz_instr]() -> u32 {
            return clz_instr.Generate();
        });
    }
}

TEST_CASE("Test ARM MSR instructions", "[JitX64]") {
    const auto is_msr_valid = [](u32 instr) -> bool {
        return Bits<18, 19>(instr) != 0;
    };

    const auto is_msr_reg_valid = [&is_msr_valid](u32 instr) -> bool {
        return is_msr_valid(instr) && Bits<0, 3>(instr) != 15;
    };

    const auto is_mrs_valid = [&](u32 inst) -> bool {
        return Bits<12, 15>(inst) != 15;
    };

    const std::array<InstructionGenerator, 3> instructions = {{
        InstructionGenerator("cccc00110010mm001111rrrrvvvvvvvv", is_msr_valid), // MSR (imm)
        InstructionGenerator("cccc00010010mm00111100000000nnnn", is_msr_reg_valid), // MSR (reg)
        InstructionGenerator("cccc000100001111dddd000000000000", is_mrs_valid), // MRS
    }};

    SECTION("Ones") {
        FuzzJitArm(1, 2, 10000, [&instructions]() -> u32 {
            return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
        });
    }

    SECTION("Fives") {
        FuzzJitArm(5, 6, 10000, [&instructions]() -> u32 {
            return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
        });
    }
}

TEST_CASE("Fuzz ARM saturated add/sub instructions", "[JitX64]") {
    auto is_valid = [](u32 inst) -> bool {
        // R15 as Rd, Rn, or Rm is UNPREDICTABLE
        return Bits<16, 19>(inst) != 0b1111 &&
               Bits<12, 15>(inst) != 0b1111 &&
               Bits<0, 3>(inst) != 0b1111;
    };

    const std::array<InstructionGenerator, 4> instructions = {{
        InstructionGenerator("cccc00010000nnnndddd00000101mmmm", is_valid), // QADD
        InstructionGenerator("cccc00010010nnnndddd00000101mmmm", is_valid), // QSUB
        InstructionGenerator("cccc00010100nnnndddd00000101mmmm", is_valid), // QDADD
        InstructionGenerator("cccc00010110nnnndddd00000101mmmm", is_valid), // QDSUB
    }};

    SECTION("Saturated") {
        FuzzJitArm(4, 5, 10000, [&instructions]() -> u32 {
            return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
        });
    }
}

TEST_CASE("Fuzz ARM saturation instructions", "[JitX64]") {
    auto is_valid = [](u32 inst) -> bool {
        // R15 as Rd or Rn is UNPREDICTABLE
        return Bits<12, 15>(inst) != 0b1111 &&
               Bits<0, 3>(inst) != 0b1111;
    };

    const std::array<InstructionGenerator, 4> instructions = {{
        InstructionGenerator("cccc0110101vvvvvddddvvvvvr01nnnn", is_valid), // SSAT
        InstructionGenerator("cccc01101010vvvvdddd11110011nnnn", is_valid), // SSAT16
        InstructionGenerator("cccc0110111vvvvvddddvvvvvr01nnnn", is_valid), // USAT
        InstructionGenerator("cccc01101110vvvvdddd11110011nnnn", is_valid), // USAT16
    }};

    FuzzJitArm(4, 5, 10000, [&instructions]() -> u32 {
        return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
    });
}

TEST_CASE("Fuzz ARM packing instructions", "[JitX64]") {
    auto is_pkh_valid = [](u32 inst) -> bool {
        // R15 as Rd, Rn, or Rm is UNPREDICTABLE
        return Bits<16, 19>(inst) != 0b1111 &&
               Bits<12, 15>(inst) != 0b1111 &&
               Bits<0, 3>(inst) != 0b1111;
    };

    const std::array<InstructionGenerator, 2> instructions = {{
        InstructionGenerator("cccc01101000nnnnddddvvvvv001mmmm", is_pkh_valid), // PKHBT
        InstructionGenerator("cccc01101000nnnnddddvvvvv101mmmm", is_pkh_valid), // PKHTB
    }};

    SECTION("Packing") {
        FuzzJitArm(1, 1, 10000, [&instructions]() -> u32 {
            return instructions[RandInt<size_t>(0, instructions.size() - 1)].Generate();
        });
    }
}

TEST_CASE("arm: Test InvalidateCacheRange", "[arm]") {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xe3a00005; // mov r0, #5
    code_mem[1] = 0xe3a0100D; // mov r1, #13
    code_mem[2] = 0xe0812000; // add r2, r1, r0
    code_mem[3] = 0xeafffffe; // b +#0 (infinite loop)

    jit.Regs() = {};
    jit.SetCpsr(0x000001d0); // User-mode

    jit.Run(4);

    REQUIRE(jit.Regs()[0] == 5);
    REQUIRE(jit.Regs()[1] == 13);
    REQUIRE(jit.Regs()[2] == 18);
    REQUIRE(jit.Regs()[15] == 0x0000000c);
    REQUIRE(jit.Cpsr() == 0x000001d0);

    // Change the code
    code_mem[1] = 0xe3a01007; // mov r1, #7
    jit.InvalidateCacheRange(/*start_memory_location = */ 4, /* length_in_bytes = */ 4);

    // Reset position of PC
    jit.Regs()[15] = 0;

    jit.Run(4);

    REQUIRE(jit.Regs()[0] == 5);
    REQUIRE(jit.Regs()[1] == 7);
    REQUIRE(jit.Regs()[2] == 12);
    REQUIRE(jit.Regs()[15] == 0x0000000c);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}
