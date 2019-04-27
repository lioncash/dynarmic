/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>

#include "common/assert.h"
#include "dynarmic/A32/config.h"
#include "frontend/A32/decoder/arm.h"
#include "frontend/A32/decoder/vfp2.h"
#include "frontend/A32/location_descriptor.h"
#include "frontend/A32/translate/translate.h"
#include "frontend/A32/translate/translate_arm/translate_arm.h"
#include "frontend/A32/types.h"
#include "frontend/ir/basic_block.h"

namespace Dynarmic::A32 {

static bool CondCanContinue(ConditionalState cond_state, const A32::IREmitter& ir) {
    ASSERT_MSG(cond_state != ConditionalState::Break, "Should never happen.");

    if (cond_state == ConditionalState::None)
        return true;

    // TODO: This is more conservative than necessary.
    return std::all_of(ir.block.begin(), ir.block.end(), [](const IR::Inst& inst) { return !inst.WritesToCPSR(); });
}

IR::Block TranslateArm(LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code, const TranslationOptions& options) {
    IR::Block block{descriptor};
    ArmTranslatorVisitor visitor{block, descriptor, options};

    bool should_continue = true;
    while (should_continue && CondCanContinue(visitor.cond_state, visitor.ir)) {
        const u32 arm_pc = visitor.ir.current_location.PC();
        const u32 arm_instruction = memory_read_code(arm_pc);

        if (const auto vfp_decoder = DecodeVFP2<ArmTranslatorVisitor>(arm_instruction)) {
            should_continue = vfp_decoder->get().call(visitor, arm_instruction);
        } else if (const auto decoder = DecodeArm<ArmTranslatorVisitor>(arm_instruction)) {
            should_continue = decoder->get().call(visitor, arm_instruction);
        } else {
            should_continue = visitor.arm_UDF();
        }

        if (visitor.cond_state == ConditionalState::Break) {
            break;
        }

        visitor.ir.current_location = visitor.ir.current_location.AdvancePC(4);
        block.CycleCount()++;
    }

    if (visitor.cond_state == ConditionalState::Translating || visitor.cond_state == ConditionalState::Trailing) {
        if (should_continue) {
            visitor.ir.SetTerm(IR::Term::LinkBlockFast{visitor.ir.current_location});
        }
    }

    ASSERT_MSG(block.HasTerminal(), "Terminal has not been set");

    block.SetEndLocation(visitor.ir.current_location);

    return block;
}

bool TranslateSingleArmInstruction(IR::Block& block, LocationDescriptor descriptor, u32 arm_instruction) {
    ArmTranslatorVisitor visitor{block, descriptor, {}};

    // TODO: Proper cond handling

    bool should_continue = true;
    if (const auto vfp_decoder = DecodeVFP2<ArmTranslatorVisitor>(arm_instruction)) {
        should_continue = vfp_decoder->get().call(visitor, arm_instruction);
    } else if (const auto decoder = DecodeArm<ArmTranslatorVisitor>(arm_instruction)) {
        should_continue = decoder->get().call(visitor, arm_instruction);
    } else {
        should_continue = visitor.arm_UDF();
    }

    // TODO: Feedback resulting cond status to caller somehow.

    visitor.ir.current_location = visitor.ir.current_location.AdvancePC(4);
    block.CycleCount()++;

    block.SetEndLocation(visitor.ir.current_location);

    return should_continue;
}

bool ArmTranslatorVisitor::ConditionPassed(Cond cond) {
    ASSERT_MSG(cond_state != ConditionalState::Break,
               "This should never happen. We requested a break but that wasn't honored.");
    if (cond == Cond::NV) {
        // NV conditional is obsolete
        ir.ExceptionRaised(Exception::UnpredictableInstruction);
        return false;
    }

    if (cond_state == ConditionalState::Translating) {
        if (ir.block.ConditionFailedLocation() != ir.current_location || cond == Cond::AL) {
            cond_state = ConditionalState::Trailing;
        } else {
            if (cond == ir.block.GetCondition()) {
                ir.block.SetConditionFailedLocation(ir.current_location.AdvancePC(4));
                ir.block.ConditionFailedCycleCount()++;
                return true;
            }

            // cond has changed, abort
            cond_state = ConditionalState::Break;
            ir.SetTerm(IR::Term::LinkBlockFast{ir.current_location});
            return false;
        }
    }

    if (cond == Cond::AL) {
        // Everything is fine with the world
        return true;
    }

    // non-AL cond

    if (!ir.block.empty()) {
        // We've already emitted instructions. Quit for now, we'll make a new block here later.
        cond_state = ConditionalState::Break;
        ir.SetTerm(IR::Term::LinkBlockFast{ir.current_location});
        return false;
    }

    // We've not emitted instructions yet.
    // We'll emit one instruction, and set the block-entry conditional appropriately.

    cond_state = ConditionalState::Translating;
    ir.block.SetCondition(cond);
    ir.block.SetConditionFailedLocation(ir.current_location.AdvancePC(4));
    ir.block.ConditionFailedCycleCount() = 1;
    return true;
}

bool ArmTranslatorVisitor::InterpretThisInstruction() {
    ir.SetTerm(IR::Term::Interpret(ir.current_location));
    return false;
}

bool ArmTranslatorVisitor::UnpredictableInstruction() {
    ir.ExceptionRaised(Exception::UnpredictableInstruction);
    ir.SetTerm(IR::Term::CheckHalt{IR::Term::ReturnToDispatch{}});
    return false;
}

bool ArmTranslatorVisitor::UndefinedInstruction() {
    ir.ExceptionRaised(Exception::UndefinedInstruction);
    ir.SetTerm(IR::Term::CheckHalt{IR::Term::ReturnToDispatch{}});
    return false;
}

IR::ResultAndCarry<IR::U32> ArmTranslatorVisitor::EmitImmShift(IR::U32 value, ShiftType type, Imm<5> imm5, IR::U1 carry_in) {
    u8 imm5_value = imm5.ZeroExtend<u8>();

    switch (type) {
    case ShiftType::LSL:
        return ir.LogicalShiftLeft(value, ir.Imm8(imm5_value), carry_in);
    case ShiftType::LSR:
        imm5_value = imm5_value ? imm5_value : 32;
        return ir.LogicalShiftRight(value, ir.Imm8(imm5_value), carry_in);
    case ShiftType::ASR:
        imm5_value = imm5_value ? imm5_value : 32;
        return ir.ArithmeticShiftRight(value, ir.Imm8(imm5_value), carry_in);
    case ShiftType::ROR:
        if (imm5_value) {
            return ir.RotateRight(value, ir.Imm8(imm5_value), carry_in);
        } else {
            return ir.RotateRightExtended(value, carry_in);
        }
    }

    UNREACHABLE();
    return {};
}

IR::ResultAndCarry<IR::U32> ArmTranslatorVisitor::EmitRegShift(IR::U32 value, ShiftType type, IR::U8 amount, IR::U1 carry_in) {
    switch (type) {
    case ShiftType::LSL:
        return ir.LogicalShiftLeft(value, amount, carry_in);
    case ShiftType::LSR:
        return ir.LogicalShiftRight(value, amount, carry_in);
    case ShiftType::ASR:
        return ir.ArithmeticShiftRight(value, amount, carry_in);
    case ShiftType::ROR:
        return ir.RotateRight(value, amount, carry_in);
    }
    UNREACHABLE();
    return {};
}

} // namespace Dynarmic::A32
