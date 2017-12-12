/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <catch.hpp>

#include <dynarmic/dynarmic.h>

#include "common/common_types.h"
#include "skyeye_interpreter/dyncom/arm_dyncom_interpreter.h"
#include "skyeye_interpreter/skyeye_common/armstate.h"

static std::array<u16, 1024> code_mem{};

static u32 MemoryRead32(u32 vaddr);
static u32 MemoryReadCode(u32 vaddr);
static void InterpreterFallback(u32 pc, Dynarmic::Jit* jit, void*);
static Dynarmic::UserCallbacks GetUserCallbacks();

static u32 MemoryRead32(u32 vaddr) {
    return vaddr;
}
static u32 MemoryReadCode(u32 vaddr) {
    if (vaddr < code_mem.size() * sizeof(u16)) {
        size_t index = vaddr / sizeof(u16);
        return code_mem[index] | (code_mem[index+1] << 16);
    }
    return 0xE7FEE7FE; //b +#0, b +#0
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

    jit->Regs() = interp_state.Reg;
    jit->SetCpsr(interp_state.Cpsr);
}

static void AddTicks(u64) {}

static Dynarmic::UserCallbacks GetUserCallbacks() {
    Dynarmic::UserCallbacks user_callbacks{};
    user_callbacks.memory.Read32 = &MemoryRead32;
    user_callbacks.memory.ReadCode = &MemoryReadCode;
    user_callbacks.InterpreterFallback = &InterpreterFallback;
    user_callbacks.AddTicks = &AddTicks;
    return user_callbacks;
}

TEST_CASE( "thumb: lsls r0, r1, #2", "[thumb]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0x0088; // lsls r0, r1, #2
    code_mem[1] = 0xE7FE; // b +#0

    jit.Regs()[0] = 1;
    jit.Regs()[1] = 2;
    jit.Regs()[15] = 0; // PC = 0
    jit.SetCpsr(0x00000030); // Thumb, User-mode

    jit.Run(1);

    REQUIRE( jit.Regs()[0] == 8 );
    REQUIRE( jit.Regs()[1] == 2 );
    REQUIRE( jit.Regs()[15] == 2 );
    REQUIRE( jit.Cpsr() == 0x00000030 );
}

TEST_CASE( "thumb: lsls r0, r1, #31", "[thumb]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0x07C8; // lsls r0, r1, #31
    code_mem[1] = 0xE7FE; // b +#0

    jit.Regs()[0] = 1;
    jit.Regs()[1] = 0xFFFFFFFF;
    jit.Regs()[15] = 0; // PC = 0
    jit.SetCpsr(0x00000030); // Thumb, User-mode

    jit.Run(1);

    REQUIRE( jit.Regs()[0] == 0x80000000 );
    REQUIRE( jit.Regs()[1] == 0xffffffff );
    REQUIRE( jit.Regs()[15] == 2 );
    REQUIRE( jit.Cpsr() == 0xA0000030 ); // N, C flags, Thumb, User-mode
}

TEST_CASE( "thumb: revsh r4, r3", "[thumb]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xBADC; // revsh r4, r3
    code_mem[1] = 0xE7FE; // b +#0

    jit.Regs()[3] = 0x12345678;
    jit.Regs()[15] = 0; // PC = 0
    jit.SetCpsr(0x00000030); // Thumb, User-mode

    jit.Run(1);

    REQUIRE( jit.Regs()[3] == 0x12345678 );
    REQUIRE( jit.Regs()[4] == 0x00007856 );
    REQUIRE( jit.Regs()[15] == 2 );
    REQUIRE( jit.Cpsr() == 0x00000030 ); // Thumb, User-mode
}

TEST_CASE( "thumb: ldr r3, [r3, #28]", "[thumb]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0x69DB; // ldr r3, [r3, #28]
    code_mem[1] = 0xE7FE; // b +#0

    jit.Regs()[3] = 0x12345678;
    jit.Regs()[15] = 0; // PC = 0
    jit.SetCpsr(0x00000030); // Thumb, User-mode

    jit.Run(1);

    REQUIRE( jit.Regs()[3] == 0x12345694 );
    REQUIRE( jit.Regs()[15] == 2 );
    REQUIRE( jit.Cpsr() == 0x00000030 ); // Thumb, User-mode
}

TEST_CASE( "thumb: blx +#67712", "[thumb]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xF010; code_mem[1] = 0xEC3E; // blx +#67712
    code_mem[2] = 0xE7FE; // b +#0

    jit.Regs()[15] = 0; // PC = 0
    jit.SetCpsr(0x00000030); // Thumb, User-mode

    jit.Run(1);

    REQUIRE( jit.Regs()[14] == (0x4 | 1) );
    REQUIRE( jit.Regs()[15] == 0x10880 );
    REQUIRE( jit.Cpsr() == 0x00000010 ); // User-mode
}

TEST_CASE( "thumb: bl +#234584", "[thumb]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xF039; code_mem[1] = 0xFA2A; // bl +#234584
    code_mem[2] = 0xE7FE; // b +#0

    jit.Regs()[15] = 0; // PC = 0
    jit.SetCpsr(0x00000030); // Thumb, User-mode

    jit.Run(1);

    REQUIRE( jit.Regs()[14] == (0x4 | 1) );
    REQUIRE( jit.Regs()[15] == 0x39458 );
    REQUIRE( jit.Cpsr() == 0x00000030 ); // Thumb, User-mode
}

TEST_CASE( "thumb: bl -#42", "[thumb]" ) {
    Dynarmic::Jit jit{GetUserCallbacks()};
    code_mem.fill({});
    code_mem[0] = 0xF7FF; code_mem[1] = 0xFFE9; // bl -#42
    code_mem[2] = 0xE7FE; // b +#0

    jit.Regs()[15] = 0; // PC = 0
    jit.SetCpsr(0x00000030); // Thumb, User-mode

    jit.Run(1);

    REQUIRE( jit.Regs()[14] == (0x4 | 1) );
    REQUIRE( jit.Regs()[15] == 0xFFFFFFD6 );
    REQUIRE( jit.Cpsr() == 0x00000030 ); // Thumb, User-mode
}
