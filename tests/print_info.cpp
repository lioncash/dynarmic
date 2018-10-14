/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>
#include <cstdlib>

#include <fmt/format.h>

#include "common/common_types.h"
#include "common/llvm_disassemble.h"
#include "frontend/A64/decoder/a64.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/translate/impl/impl.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"

using namespace Dynarmic;

const char* GetNameOfInstruction(u32 instruction) {
    if (auto decoder = A64::Decode<A64::TranslatorVisitor>(instruction)) {
        return decoder->get().GetName();
    }
    return "<null>";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fmt::print("usage: {} <instruction_in_hex>\n", argv[0]);
        return 1;
    }

    if (strlen(argv[1]) > 8) {
        fmt::print("hex string too long\n");
        return 1;
    }

    const u32 instruction = strtol(argv[1], nullptr, 16);
    fmt::print("{:08x} {}\n", instruction, Common::DisassembleAArch64(instruction));
    fmt::print("Name: {}\n", GetNameOfInstruction(instruction));

    const A64::LocationDescriptor location{0, {}};
    IR::Block block{location};
    const bool should_continue = A64::TranslateSingleInstruction(block, location, instruction);
    fmt::print("should_continue: {}\n", should_continue);
    fmt::print("IR:\n");
    fmt::print("{}\n", IR::DumpBlock(block));

    return 0;
}
