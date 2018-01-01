/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "frontend/A32/ir_emitter.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic {
namespace A32 {

using Opcode = IR::Opcode;

u32 IREmitter::PC() {
    u32 offset = current_location.TFlag() ? 4 : 8;
    return current_location.PC() + offset;
}

u32 IREmitter::AlignPC(size_t alignment) {
    u32 pc = PC();
    return static_cast<u32>(pc - pc % alignment);
}

IR::Value IREmitter::GetRegister(A32::Reg reg) {
    if (reg == A32::Reg::PC) {
        return Imm32(PC());
    }
    return Inst(Opcode::GetRegister, { IR::Value(reg) });
}

IR::Value IREmitter::GetExtendedRegister(A32::ExtReg reg) {
    if (A32::IsSingleExtReg(reg)) {
        return Inst(Opcode::GetExtendedRegister32, {IR::Value(reg)});
    }

    if (A32::IsDoubleExtReg(reg)) {
        return Inst(Opcode::GetExtendedRegister64, {IR::Value(reg)});
    }

    ASSERT_MSG(false, "Invalid reg.");
}

void IREmitter::SetRegister(const A32::Reg reg, const IR::Value& value) {
    ASSERT(reg != A32::Reg::PC);
    Inst(Opcode::SetRegister, { IR::Value(reg), value });
}

void IREmitter::SetExtendedRegister(const A32::ExtReg reg, const IR::Value& value) {
    if (A32::IsSingleExtReg(reg)) {
        Inst(Opcode::SetExtendedRegister32, {IR::Value(reg), value});
    } else if (A32::IsDoubleExtReg(reg)) {
        Inst(Opcode::SetExtendedRegister64, {IR::Value(reg), value});
    } else {
        ASSERT_MSG(false, "Invalid reg.");
    }
}

void IREmitter::ALUWritePC(const IR::Value& value) {
    // This behaviour is ARM version-dependent.
    // The below implementation is for ARMv6k
    BranchWritePC(value);
}

void IREmitter::BranchWritePC(const IR::Value& value) {
    if (!current_location.TFlag()) {
        auto new_pc = And(value, Imm32(0xFFFFFFFC));
        Inst(Opcode::SetRegister, { IR::Value(A32::Reg::PC), new_pc });
    } else {
        auto new_pc = And(value, Imm32(0xFFFFFFFE));
        Inst(Opcode::SetRegister, { IR::Value(A32::Reg::PC), new_pc });
    }
}

void IREmitter::BXWritePC(const IR::Value& value) {
    Inst(Opcode::BXWritePC, {value});
}

void IREmitter::LoadWritePC(const IR::Value& value) {
    // This behaviour is ARM version-dependent.
    // The below implementation is for ARMv6k
    BXWritePC(value);
}

void IREmitter::CallSupervisor(const IR::Value& value) {
    Inst(Opcode::CallSupervisor, {value});
}

void IREmitter::PushRSB(const A32::LocationDescriptor& return_location) {
    Inst(Opcode::PushRSB, {IR::Value(return_location.UniqueHash())});
}

IR::Value IREmitter::GetCpsr() {
    return Inst(Opcode::GetCpsr, {});
}

void IREmitter::SetCpsr(const IR::Value& value) {
    Inst(Opcode::SetCpsr, {value});
}

void IREmitter::SetCpsrNZCV(const IR::Value& value) {
    Inst(Opcode::SetCpsrNZCV, {value});
}

void IREmitter::SetCpsrNZCVQ(const IR::Value& value) {
    Inst(Opcode::SetCpsrNZCVQ, {value});
}

IR::Value IREmitter::GetCFlag() {
    return Inst(Opcode::GetCFlag, {});
}

void IREmitter::SetNFlag(const IR::Value& value) {
    Inst(Opcode::SetNFlag, {value});
}

void IREmitter::SetZFlag(const IR::Value& value) {
    Inst(Opcode::SetZFlag, {value});
}

void IREmitter::SetCFlag(const IR::Value& value) {
    Inst(Opcode::SetCFlag, {value});
}

void IREmitter::SetVFlag(const IR::Value& value) {
    Inst(Opcode::SetVFlag, {value});
}

void IREmitter::OrQFlag(const IR::Value& value) {
    Inst(Opcode::OrQFlag, {value});
}

IR::Value IREmitter::GetGEFlags() {
    return Inst(Opcode::GetGEFlags, {});
}

void IREmitter::SetGEFlags(const IR::Value& value) {
    Inst(Opcode::SetGEFlags, {value});
}

void IREmitter::SetGEFlagsCompressed(const IR::Value& value) {
    Inst(Opcode::SetGEFlagsCompressed, {value});
}

IR::Value IREmitter::GetFpscr() {
    return Inst(Opcode::GetFpscr, {});
}

void IREmitter::SetFpscr(const IR::Value& new_fpscr) {
    Inst(Opcode::SetFpscr, {new_fpscr});
}

IR::Value IREmitter::GetFpscrNZCV() {
    return Inst(Opcode::GetFpscrNZCV, {});
}

void IREmitter::SetFpscrNZCV(const IR::Value& new_fpscr_nzcv) {
    Inst(Opcode::SetFpscrNZCV, {new_fpscr_nzcv});
}

void IREmitter::ClearExclusive() {
    Inst(Opcode::ClearExclusive, {});
}

void IREmitter::SetExclusive(const IR::Value& vaddr, size_t byte_size) {
    ASSERT(byte_size == 1 || byte_size == 2 || byte_size == 4 || byte_size == 8 || byte_size == 16);
    Inst(Opcode::SetExclusive, {vaddr, Imm8(u8(byte_size))});
}

IR::Value IREmitter::ReadMemory8(const IR::Value& vaddr) {
    return Inst(Opcode::ReadMemory8, {vaddr});
}

IR::Value IREmitter::ReadMemory16(const IR::Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory16, {vaddr});
    return current_location.EFlag() ? ByteReverseHalf(value) : value;
}

IR::Value IREmitter::ReadMemory32(const IR::Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory32, {vaddr});
    return current_location.EFlag() ? ByteReverseWord(value) : value;
}

IR::Value IREmitter::ReadMemory64(const IR::Value& vaddr) {
    auto value = Inst(Opcode::ReadMemory64, {vaddr});
    return current_location.EFlag() ? ByteReverseDual(value) : value;
}

void IREmitter::WriteMemory8(const IR::Value& vaddr, const IR::Value& value) {
    Inst(Opcode::WriteMemory8, {vaddr, value});
}

void IREmitter::WriteMemory16(const IR::Value& vaddr, const IR::Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseHalf(value);
        Inst(Opcode::WriteMemory16, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory16, {vaddr, value});
    }
}

void IREmitter::WriteMemory32(const IR::Value& vaddr, const IR::Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseWord(value);
        Inst(Opcode::WriteMemory32, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory32, {vaddr, value});
    }
}

void IREmitter::WriteMemory64(const IR::Value& vaddr, const IR::Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseDual(value);
        Inst(Opcode::WriteMemory64, {vaddr, v});
    } else {
        Inst(Opcode::WriteMemory64, {vaddr, value});
    }
}

IR::Value IREmitter::ExclusiveWriteMemory8(const IR::Value& vaddr, const IR::Value& value) {
    return Inst(Opcode::ExclusiveWriteMemory8, {vaddr, value});
}

IR::Value IREmitter::ExclusiveWriteMemory16(const IR::Value& vaddr, const IR::Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseHalf(value);
        return Inst(Opcode::ExclusiveWriteMemory16, {vaddr, v});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory16, {vaddr, value});
    }
}

IR::Value IREmitter::ExclusiveWriteMemory32(const IR::Value& vaddr, const IR::Value& value) {
    if (current_location.EFlag()) {
        auto v = ByteReverseWord(value);
        return Inst(Opcode::ExclusiveWriteMemory32, {vaddr, v});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory32, {vaddr, value});
    }
}

IR::Value IREmitter::ExclusiveWriteMemory64(const IR::Value& vaddr, const IR::Value& value_lo, const IR::Value& value_hi) {
    if (current_location.EFlag()) {
        auto vlo = ByteReverseWord(value_lo);
        auto vhi = ByteReverseWord(value_hi);
        return Inst(Opcode::ExclusiveWriteMemory64, {vaddr, vlo, vhi});
    } else {
        return Inst(Opcode::ExclusiveWriteMemory64, {vaddr, value_lo, value_hi});
    }
}

void IREmitter::CoprocInternalOperation(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRd, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    Inst(Opcode::CoprocInternalOperation, {IR::Value(coproc_info)});
}

void IREmitter::CoprocSendOneWord(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2, const IR::Value& word) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    Inst(Opcode::CoprocSendOneWord, {IR::Value(coproc_info), word});
}

void IREmitter::CoprocSendTwoWords(size_t coproc_no, bool two, size_t opc, A32::CoprocReg CRm, const IR::Value& word1, const IR::Value& word2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc),
                                  static_cast<u8>(CRm)};
    Inst(Opcode::CoprocSendTwoWords, {IR::Value(coproc_info), word1, word2});
}

IR::Value IREmitter::CoprocGetOneWord(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc1),
                                  static_cast<u8>(CRn),
                                  static_cast<u8>(CRm),
                                  static_cast<u8>(opc2)};
    return Inst(Opcode::CoprocGetOneWord, {IR::Value(coproc_info)});
}

IR::Value IREmitter::CoprocGetTwoWords(size_t coproc_no, bool two, size_t opc, A32::CoprocReg CRm) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(opc),
                                  static_cast<u8>(CRm)};
    return Inst(Opcode::CoprocGetTwoWords, {IR::Value(coproc_info)});
}

void IREmitter::CoprocLoadWords(size_t coproc_no, bool two, bool long_transfer, A32::CoprocReg CRd, const IR::Value& address, bool has_option, u8 option) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(long_transfer ? 1 : 0),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(has_option ? 1 : 0),
                                  static_cast<u8>(option)};
    Inst(Opcode::CoprocLoadWords, {IR::Value(coproc_info), address});
}

void IREmitter::CoprocStoreWords(size_t coproc_no, bool two, bool long_transfer, A32::CoprocReg CRd, const IR::Value& address, bool has_option, u8 option) {
    ASSERT(coproc_no <= 15);
    std::array<u8, 8> coproc_info{static_cast<u8>(coproc_no),
                                  static_cast<u8>(two ? 1 : 0),
                                  static_cast<u8>(long_transfer ? 1 : 0),
                                  static_cast<u8>(CRd),
                                  static_cast<u8>(has_option ? 1 : 0),
                                  static_cast<u8>(option)};
    Inst(Opcode::CoprocStoreWords, {IR::Value(coproc_info), address});
}

} // namespace IR
} // namespace Dynarmic
