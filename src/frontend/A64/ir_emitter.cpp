/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "common/assert.h"
#include "frontend/A64/ir_emitter.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic {
namespace A64 {

using Opcode = IR::Opcode;

u64 IREmitter::PC() {
    return current_location.PC();
}

u64 IREmitter::AlignPC(size_t alignment) {
    u64 pc = PC();
    return static_cast<u64>(pc - pc % alignment);
}

void IREmitter::SetCheckBit(const IR::U1& value) {
    Inst(Opcode::A64SetCheckBit, value);
}

IR::U1 IREmitter::GetCFlag() {
    return Inst<IR::U1>(Opcode::A64GetCFlag);
}

void IREmitter::SetNZCV(const IR::NZCV& nzcv) {
    Inst(Opcode::A64SetNZCV, nzcv);
}

void IREmitter::CallSupervisor(u32 imm) {
    Inst(Opcode::A64CallSupervisor, Imm32(imm));
}

IR::U32 IREmitter::GetW(Reg reg) {
    if (reg == Reg::ZR)
        return Imm32(0);
    return Inst<IR::U32>(Opcode::A64GetW, IR::Value(reg));
}

IR::U64 IREmitter::GetX(Reg reg) {
    if (reg == Reg::ZR)
        return Imm64(0);
    return Inst<IR::U64>(Opcode::A64GetX, IR::Value(reg));
}

IR::U64 IREmitter::GetSP() {
    return Inst<IR::U64>(Opcode::A64GetSP);
}

void IREmitter::SetW(const Reg reg, const IR::U32& value) {
    if (reg == Reg::ZR)
        return;
    Inst(Opcode::A64SetW, IR::Value(reg), value);
}

void IREmitter::SetX(const Reg reg, const IR::U64& value) {
    if (reg == Reg::ZR)
        return;
    Inst(Opcode::A64SetX, IR::Value(reg), value);
}

void IREmitter::SetSP(const IR::U64& value) {
    Inst(Opcode::A64SetSP, value);
}

void IREmitter::SetPC(const IR::U64& value) {
    Inst(Opcode::A64SetPC, value);
}

} // namespace IR
} // namespace Dynarmic
