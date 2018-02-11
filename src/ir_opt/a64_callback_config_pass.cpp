/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include "dynarmic/A64/config.h"
#include "frontend/A64/ir_emitter.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"
#include "ir_opt/passes.h"

namespace Dynarmic::Optimization {

void A64CallbackConfigPass(IR::Block& block, const A64::UserConfig& conf) {
    if (conf.hook_data_cache_operations) {
        return;
    }

    for (auto& inst : block) {
        if (inst.GetOpcode() != IR::Opcode::A64DataCacheOperationRaised) {
            continue;
        }

        auto op = static_cast<A64::DataCacheOperation>(inst.GetArg(0).GetU64());
        if (op == A64::DataCacheOperation::ZeroByVA) {
            A64::IREmitter ir{block};
            ir.SetInsertionPoint(&inst);

            size_t bytes = 4 << static_cast<size_t>(conf.dczid_el0 & 0b1111);
            IR::U64 addr{inst.GetArg(1)};

            const IR::U128 zero_u128 = ir.ZeroExtendToQuad(ir.Imm64(0));
            while (bytes >= 16) {
                ir.WriteMemory128(addr, zero_u128);
                addr = ir.Add(addr, ir.Imm64(16));
                bytes -= 16;
            }

            while (bytes >= 8) {
                ir.WriteMemory64(addr, ir.Imm64(0));
                addr = ir.Add(addr, ir.Imm64(8));
                bytes -= 8;
            }

            while (bytes >= 4) {
                ir.WriteMemory32(addr, ir.Imm32(0));
                addr = ir.Add(addr, ir.Imm64(4));
                bytes -= 4;
            }
        }
        inst.Invalidate();
    }
}

} // namespace Dynarmic::Optimization
