/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <dynarmic/A32/config.h>

#include "frontend/ir/basic_block.h"
#include "frontend/ir/opcodes.h"
#include "ir_opt/passes.h"

namespace Dynarmic::Optimization {

void A32ConstantMemoryReads(IR::Block& block, A32::UserCallbacks* cb) {
    for (auto& inst : block) {
        switch (inst.GetOpcode()) {
        case IR::Opcode::A32SetCFlag: {
            const IR::Value arg = inst.GetArg(0);
            if (!arg.IsImmediate() && arg.GetInst()->GetOpcode() == IR::Opcode::A32GetCFlag) {
                inst.Invalidate();
            }
            break;
        }
        case IR::Opcode::A32ReadMemory8: {
            if (!inst.AreAllArgsImmediates()) {
                break;
            }

            const u32 vaddr = inst.GetArg(0).GetU32();
            if (cb->IsReadOnlyMemory(vaddr)) {
                const u8 value_from_memory = cb->MemoryRead8(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
            }
            break;
        }
        case IR::Opcode::A32ReadMemory16: {
            if (!inst.AreAllArgsImmediates()) {
                break;
            }

            const u32 vaddr = inst.GetArg(0).GetU32();
            if (cb->IsReadOnlyMemory(vaddr)) {
                const u16 value_from_memory = cb->MemoryRead16(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
            }
            break;
        }
        case IR::Opcode::A32ReadMemory32: {
            if (!inst.AreAllArgsImmediates()) {
                break;
            }

            const u32 vaddr = inst.GetArg(0).GetU32();
            if (cb->IsReadOnlyMemory(vaddr)) {
                const u32 value_from_memory = cb->MemoryRead32(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
            }
            break;
        }
        case IR::Opcode::A32ReadMemory64: {
            if (!inst.AreAllArgsImmediates()) {
                break;
            }

            const u32 vaddr = inst.GetArg(0).GetU32();
            if (cb->IsReadOnlyMemory(vaddr)) {
                const u64 value_from_memory = cb->MemoryRead64(vaddr);
                inst.ReplaceUsesWith(IR::Value{value_from_memory});
            }
            break;
        }
        default:
            break;
        }
    }
}

} // namespace Dynarmic::Optimization
