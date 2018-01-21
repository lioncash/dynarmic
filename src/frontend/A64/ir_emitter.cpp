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

void IREmitter::ExceptionRaised(Exception exception) {
    Inst(Opcode::A64ExceptionRaised, Imm64(PC()), Imm64(static_cast<u64>(exception)));
}

IR::U8 IREmitter::ReadMemory8(const IR::U64& vaddr) {
    return Inst<IR::U8>(Opcode::A64ReadMemory8, vaddr);
}

IR::U16 IREmitter::ReadMemory16(const IR::U64& vaddr) {
    return Inst<IR::U16>(Opcode::A64ReadMemory16, vaddr);
}

IR::U32 IREmitter::ReadMemory32(const IR::U64& vaddr) {
    return Inst<IR::U32>(Opcode::A64ReadMemory32, vaddr);
}

IR::U64 IREmitter::ReadMemory64(const IR::U64& vaddr) {
    return Inst<IR::U64>(Opcode::A64ReadMemory64, vaddr);
}

void IREmitter::WriteMemory8(const IR::U64& vaddr, const IR::U8& value) {
    Inst(Opcode::A64WriteMemory8, vaddr, value);
}

void IREmitter::WriteMemory16(const IR::U64& vaddr, const IR::U16& value) {
    Inst(Opcode::A64WriteMemory16, vaddr, value);
}

void IREmitter::WriteMemory32(const IR::U64& vaddr, const IR::U32& value) {
    Inst(Opcode::A64WriteMemory32, vaddr, value);
}

void IREmitter::WriteMemory64(const IR::U64& vaddr, const IR::U64& value) {
    Inst(Opcode::A64WriteMemory64, vaddr, value);
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

IR::U128 IREmitter::GetD(Vec vec) {
    return Inst<IR::U128>(Opcode::A64GetD, IR::Value(vec));
}

IR::U128 IREmitter::GetQ(Vec vec) {
    return Inst<IR::U128>(Opcode::A64GetQ, IR::Value(vec));
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

void IREmitter::SetD(const Vec vec, const IR::U128& value) {
    Inst(Opcode::A64SetD, IR::Value(vec), value);
}

void IREmitter::SetQ(const Vec vec, const IR::U128& value) {
    Inst(Opcode::A64SetQ, IR::Value(vec), value);
}

void IREmitter::SetSP(const IR::U64& value) {
    Inst(Opcode::A64SetSP, value);
}

void IREmitter::SetPC(const IR::U64& value) {
    Inst(Opcode::A64SetPC, value);
}

} // namespace IR
} // namespace Dynarmic
