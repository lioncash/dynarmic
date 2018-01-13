/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "inst_gen.h"
#include "rand_int.h"

InstructionGenerator::InstructionGenerator(const char* format){
    ASSERT(std::strlen(format) == 32);

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

u32 InstructionGenerator::Generate() const {
    u32 inst;
    do {
        u32 random = RandInt<u32>(0, 0xFFFFFFFF);
        inst = bits | (random & ~mask);
    } while (IsInvalidInstruction(inst));
    return inst;
}

std::vector<InstructionGenerator> InstructionGenerator::invalid_instructions;
