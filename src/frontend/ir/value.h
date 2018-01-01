/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/common_types.h"
#include "frontend/A32/types.h"

namespace Dynarmic {
namespace IR {

class Inst;

/**
 * A representation of a value in the IR.
 * A value may either be an immediate or the result of a microinstruction.
 */
class Value final {
public:
    Value() : type(Type::Void) {}
    explicit Value(Inst* value);
    explicit Value(A32::Reg value);
    explicit Value(A32::ExtReg value);
    explicit Value(bool value);
    explicit Value(u8 value);
    explicit Value(u16 value);
    explicit Value(u32 value);
    explicit Value(u64 value);
    explicit Value(std::array<u8, 8> value);

    bool IsEmpty() const;
    bool IsImmediate() const;
    Type GetType() const;

    Inst* GetInst() const;
    A32::Reg GetA32RegRef() const;
    A32::ExtReg GetA32ExtRegRef() const;
    bool GetU1() const;
    u8 GetU8() const;
    u16 GetU16() const;
    u32 GetU32() const;
    u64 GetU64() const;
    std::array<u8, 8> GetCoprocInfo() const;

private:
    Type type;

    union {
        Inst* inst; // type == Type::Opaque
        A32::Reg imm_a32regref;
        A32::ExtReg imm_a32extregref;
        bool imm_u1;
        u8 imm_u8;
        u16 imm_u16;
        u32 imm_u32;
        u64 imm_u64;
        std::array<u8, 8> imm_coproc;
    } inner;
};
static_assert(sizeof(Value) <= 2 * sizeof(u64), "IR::Value should be kept small in size");

} // namespace IR
} // namespace Dynarmic
