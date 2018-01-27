/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <iosfwd>
#include <string>

#include "common/common_types.h"

namespace Dynarmic::IR {

/**
 * The Opcodes of our intermediate representation.
 * Type signatures for each opcode can be found in opcodes.inc
 */
enum class Opcode {
#define OPCODE(name, type, ...) name,
#define A32OPC(name, type, ...) A32##name,
#define A64OPC(name, type, ...) A64##name,
#include "opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
    NUM_OPCODE
};

constexpr size_t OpcodeCount = static_cast<size_t>(Opcode::NUM_OPCODE);

/**
 * The intermediate representation is typed. These are the used by our IR.
 */
enum class Type {
    Void = 0,
    A32Reg = 1 << 0,
    A32ExtReg = 1 << 1,
    A64Reg = 1 << 2,
    A64Vec = 1 << 3,
    Opaque = 1 << 4,
    U1 = 1 << 5,
    U8 = 1 << 6,
    U16 = 1 << 7,
    U32 = 1 << 8,
    U64 = 1 << 9,
    U128 = 1 << 10,
    CoprocInfo = 1 << 11,
    NZCVFlags = 1 << 12,
    Cond = 1 << 13,
};

constexpr Type operator|(Type a, Type b) {
    return static_cast<Type>(static_cast<size_t>(a) | static_cast<size_t>(b));
}

constexpr Type operator&(Type a, Type b) {
    return static_cast<Type>(static_cast<size_t>(a) & static_cast<size_t>(b));
}

/// Get return type of an opcode
Type GetTypeOf(Opcode op);

/// Get the number of arguments an opcode accepts
size_t GetNumArgsOf(Opcode op);

/// Get the required type of an argument of an opcode
Type GetArgTypeOf(Opcode op, size_t arg_index);

/// Get the name of an opcode.
std::string GetNameOf(Opcode op);

/// Get the name of a type.
std::string GetNameOf(Type type);

/// @returns true if t1 and t2 are compatible types
bool AreTypesCompatible(Type t1, Type t2);

std::ostream& operator<<(std::ostream& o, Opcode opcode);
std::ostream& operator<<(std::ostream& o, Type type);

} // namespace Dynarmic::IR
