/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/common_types.h"

namespace Dynarmic {
namespace IR {

/**
 * The Opcodes of our intermediate representation.
 * Type signatures for each opcode can be found in opcodes.inc
 */
enum class Opcode {
#define OPCODE(name, type, ...) name,
#define A32OPC(name, type, ...) A32##name,
#include "opcodes.inc"
#undef OPCODE
#undef A32OPC
    NUM_OPCODE
};

constexpr size_t OpcodeCount = static_cast<size_t>(Opcode::NUM_OPCODE);

/**
 * The intermediate representation is typed. These are the used by our IR.
 */
enum class Type {
    Void,
    A32Reg,
    A32ExtReg,
    A64Reg,
    A64Vec,
    Opaque,
    U1,
    U8,
    U16,
    U32,
    U64,
    F32,
    F64,
    CoprocInfo,
};

/// Get return type of an opcode
Type GetTypeOf(Opcode op);

/// Get the number of arguments an opcode accepts
size_t GetNumArgsOf(Opcode op);

/// Get the required type of an argument of an opcode
Type GetArgTypeOf(Opcode op, size_t arg_index);

/// Get the name of an opcode.
const char* GetNameOf(Opcode op);

/// Get the name of a type.
const char* GetNameOf(Type type);

/// @returns true if t1 and t2 are compatible types
bool AreTypesCompatible(Type t1, Type t2);

} // namespace Arm
} // namespace Dynarmic
