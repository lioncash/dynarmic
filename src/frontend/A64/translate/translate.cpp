/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "frontend/A64/decoder/a64.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/translate/impl/impl.h"
#include "frontend/A64/translate/translate.h"
#include "frontend/ir/basic_block.h"

namespace Dynarmic::A64 {

IR::Block Translate(LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code, TranslationOptions options) {
    IR::Block block{descriptor};
    TranslatorVisitor visitor{block, descriptor, std::move(options)};

    bool should_continue = true;
    while (should_continue) {
        const u64 pc = visitor.ir.current_location->PC();
        const u32 instruction = memory_read_code(pc);

        if (auto decoder = Decode<TranslatorVisitor>(instruction)) {
            should_continue = decoder->get().call(visitor, instruction);
        } else {
            should_continue = visitor.InterpretThisInstruction();
        }

        visitor.ir.current_location = visitor.ir.current_location->AdvancePC(4);
        block.CycleCount()++;
    }

    ASSERT_MSG(block.HasTerminal(), "Terminal has not been set");

    block.SetEndLocation(*visitor.ir.current_location);

    return block;
}

bool TranslateSingleInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction) {
    TranslatorVisitor visitor{block, descriptor, {}};

    bool should_continue = true;
    if (auto decoder = Decode<TranslatorVisitor>(instruction)) {
        should_continue = decoder->get().call(visitor, instruction);
    } else {
        should_continue = visitor.InterpretThisInstruction();
    }

    visitor.ir.current_location = visitor.ir.current_location->AdvancePC(4);
    block.CycleCount()++;

    block.SetEndLocation(*visitor.ir.current_location);

    return should_continue;
}

} // namespace Dynarmic::A64
