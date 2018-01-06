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

IR::U32 IREmitter::GetW(Reg reg) {
    return Inst<IR::U32>(Opcode::A64GetW, IR::Value(reg));
}

IR::U64 IREmitter::GetX(Reg reg) {
    return Inst<IR::U64>(Opcode::A64GetX, IR::Value(reg));
}

void IREmitter::SetW(const Reg reg, const IR::U32& value) {
    Inst(Opcode::A64SetW, IR::Value(reg), value);
}

void IREmitter::SetX(const Reg reg, const IR::U64& value) {
    Inst(Opcode::A64SetX, IR::Value(reg), value);
}

} // namespace IR
} // namespace Dynarmic
