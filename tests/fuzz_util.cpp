/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "common/assert.h"
#include "common/fp/fpcr.h"
#include "common/fp/rounding_mode.h"
#include "fuzz_util.h"
#include "rand_int.h"

using namespace Dynarmic;

std::ostream& operator<<(std::ostream& o, Vector vec) {
    return o << fmt::format("{:016x}'{:016x}", vec[1], vec[0]);
}

Vector RandomVector() {
    return {RandInt<u64>(0, ~u64(0)), RandInt<u64>(0, ~u64(0))};
}

u32 RandomFpcr() {
    FP::FPCR fpcr;
    fpcr.AHP(RandInt(0, 1) == 0);
    fpcr.DN(RandInt(0, 1) == 0);
    fpcr.FZ(RandInt(0, 1) == 0);
    fpcr.RMode(static_cast<FP::RoundingMode>(RandInt(0, 3)));
    fpcr.FZ16(RandInt(0, 1) == 0);
    return fpcr.Value();
}

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
    const u32 random = RandInt<u32>(0, 0xFFFFFFFF);
    return bits | (random & ~mask);
}
