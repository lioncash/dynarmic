/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include <boost/variant/get.hpp>

#include "common/assert.h"
#include "common/common_types.h"
#include "dynarmic/A64/config.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"
#include "ir_opt/passes.h"

namespace Dynarmic {
namespace Optimization {

void A64MergeInterpretBlocksPass(IR::Block& block, A64::UserCallbacks* cb) {
    const auto is_interpret_instruction = [cb](A64::LocationDescriptor location) {
        const u32 instruction = cb->MemoryReadCode(location.PC());

        IR::Block new_block{location};
        A64::TranslateSingleInstruction(new_block, location, instruction);

        if (!new_block.Instructions().empty())
            return false;

        const IR::Terminal terminal = new_block.GetTerminal();
        if (auto term = boost::get<IR::Term::Interpret>(&terminal)) {
            printf("INTERPET %08x \\x%02x\\x%02x\\x%02x\\x%02x \n", instruction, u8(instruction >> 0), u8(instruction >> 8), u8(instruction >> 16), u8(instruction >> 24));
            return term->next == location;
        }

        return false;
    };

    IR::Terminal terminal = block.GetTerminal();
    auto term = boost::get<IR::Term::Interpret>(&terminal);
    if (!term)
        return;

    A64::LocationDescriptor location{term->next};
    size_t num_instructions = 1;

    while (is_interpret_instruction(location.AdvancePC(static_cast<int>(num_instructions * 4)))) {
        num_instructions++;
    }

    printf("%zu\n", num_instructions);

    term->num_instructions = num_instructions;
    block.ReplaceTerminal(terminal);
    block.CycleCount() += num_instructions - 1;
}

} // namespace Optimization
} // namespace Dynarmic
