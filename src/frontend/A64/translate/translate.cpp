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

namespace Dynarmic {
namespace A64 {

IR::Block Translate(LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code) {
    TranslatorVisitor visitor{descriptor};

    bool should_continue = true;
    while (should_continue) {
        const u64 pc = visitor.ir.current_location.PC();
        const u32 instruction = memory_read_code(pc);

        if (auto decoder = Decode<TranslatorVisitor>(instruction)) {
            should_continue = decoder->call(visitor, instruction);
        } else {
            should_continue = visitor.InterpretThisInstruction();
        }

        visitor.ir.current_location = visitor.ir.current_location.AdvancePC(4);
        visitor.ir.block.CycleCount()++;
    }

    ASSERT_MSG(visitor.ir.block.HasTerminal(), "Terminal has not been set");

    visitor.ir.block.SetEndLocation(visitor.ir.current_location);

    return std::move(visitor.ir.block);
}

} // namespace A64
} // namespace Dynarmic
