/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include "common/assert.h"
#include "common/common_types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/ir_emitter.h"
#include "frontend/ir/value.h"
#include "ir_opt/passes.h"

namespace Dynarmic::Optimization {

void A64GetSetElimination(IR::Block& block) {
    using Iterator = IR::Block::iterator;
    struct RegisterInfo {
        IR::Value register_value;
        bool set_instruction_present = false;
        Iterator last_set_instruction;
    };
    std::array<RegisterInfo, 31> reg_info;
    std::array<RegisterInfo, 32> vec_info;
    RegisterInfo sp_info;
    RegisterInfo nzcv_info;

    const auto do_set = [&block](RegisterInfo& info, IR::Value value, Iterator set_inst) {
        if (info.set_instruction_present) {
            info.last_set_instruction->Invalidate();
            block.Instructions().erase(info.last_set_instruction);
        }

        info.register_value = value;
        info.set_instruction_present = true;
        info.last_set_instruction = set_inst;
    };

    const auto do_get = [&block](RegisterInfo& info, Iterator get_inst) {
        if (info.register_value.IsEmpty()) {
            info.register_value = IR::Value(&*get_inst);
            return;
        }

        if (!info.set_instruction_present) {
            static const std::vector<IR::Opcode> ordering {
                IR::Opcode::A64GetW,
                IR::Opcode::A64GetX,
                IR::Opcode::A64GetS,
                IR::Opcode::A64GetD,
                IR::Opcode::A64GetQ,
            };
            const auto source_order = std::find(ordering.begin(), ordering.end(), info.register_value.GetInst()->GetOpcode());
            const auto dest_order = std::find(ordering.begin(), ordering.end(), get_inst->GetOpcode());
            if (source_order < dest_order) {
                // Zero extension of the value is not appropriate in this case.
                // Replace currently known value with the new value.
                info.register_value = IR::Value(&*get_inst);
                return;
            }
        }

        if (get_inst->GetType() == info.register_value.GetType()) {
            get_inst->ReplaceUsesWith(info.register_value);
            return;
        }

        const IR::Value replacement = [&]() -> IR::Value {
            IR::IREmitter ir{block};
            ir.SetInsertionPoint(get_inst);

            const IR::UAny value_to_convert{info.register_value};
            switch (get_inst->GetType()) {
            case IR::Type::U8:
                return ir.LeastSignificantByte(ir.ZeroExtendToWord(value_to_convert));
            case IR::Type::U16:
                return ir.LeastSignificantHalf(ir.ZeroExtendToWord(value_to_convert));
            case IR::Type::U32:
                return ir.ZeroExtendToWord(value_to_convert);
            case IR::Type::U64:
                return ir.ZeroExtendToLong(value_to_convert);
            case IR::Type::U128:
                return ir.ZeroExtendToQuad(value_to_convert);
            default:
                UNREACHABLE();
                return {};
            }
        }();
        get_inst->ReplaceUsesWith(replacement);
    };

    for (auto inst = block.begin(); inst != block.end(); ++inst) {
        switch (inst->GetOpcode()) {
        case IR::Opcode::A64GetW:
        case IR::Opcode::A64GetX: {
            const size_t index = A64::RegNumber(inst->GetArg(0).GetA64RegRef());
            do_get(reg_info.at(index), inst);
            break;
        }
        case IR::Opcode::A64GetS:
        case IR::Opcode::A64GetD:
        case IR::Opcode::A64GetQ: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_get(vec_info.at(index), inst);
            break;
        }
        case IR::Opcode::A64GetSP: {
            do_get(sp_info, inst);
            break;
        }
        case IR::Opcode::A64SetW:
        case IR::Opcode::A64SetX: {
            const size_t index = A64::RegNumber(inst->GetArg(0).GetA64RegRef());
            do_set(reg_info.at(index), inst->GetArg(1), inst);
            break;
        }
        case IR::Opcode::A64SetS:
        case IR::Opcode::A64SetD:
        case IR::Opcode::A64SetQ: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_set(vec_info.at(index), inst->GetArg(1), inst);
            break;
        }
        case IR::Opcode::A64SetSP: {
            do_set(sp_info, inst->GetArg(0), inst);
            break;
        }
        case IR::Opcode::A64SetNZCV: {
            do_set(nzcv_info, inst->GetArg(0), inst);
            break;
        }
        default: {
            if (inst->ReadsFromCPSR() || inst->WritesToCPSR()) {
                nzcv_info = {};
            }
            if (inst->ReadsFromCoreRegister() || inst->WritesToCoreRegister()) {
                reg_info = {};
                vec_info = {};
                sp_info = {};
            }
            break;
        }
        }
    }
}

} // namespace Dynarmic::Optimization
