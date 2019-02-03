/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "frontend/ir/opcodes.h"
#include "frontend/ir/type.h"

namespace Dynarmic::IR {

// Opcode information

namespace OpcodeInfo {

struct Meta {
    const char* name;
    Type type;
    std::vector<Type> arg_types;
};

constexpr Type Void = Type::Void;
constexpr Type A32Reg = Type::A32Reg;
constexpr Type A32ExtReg = Type::A32ExtReg;
constexpr Type A64Reg = Type::A64Reg;
constexpr Type A64Vec = Type::A64Vec;
constexpr Type Opaque = Type::Opaque;
constexpr Type U1 = Type::U1;
constexpr Type U8 = Type::U8;
constexpr Type U16 = Type::U16;
constexpr Type U32 = Type::U32;
constexpr Type U64 = Type::U64;
constexpr Type U128 = Type::U128;
constexpr Type CoprocInfo = Type::CoprocInfo;
constexpr Type NZCV = Type::NZCVFlags;
constexpr Type Cond = Type::Cond;
constexpr Type Table = Type::Table;

static const std::vector<Meta> opcode_info {
#define OPCODE(name, type, ...) { #name, type, { __VA_ARGS__ } },
#define A32OPC(name, type, ...) { #name, type, { __VA_ARGS__ } },
#define A64OPC(name, type, ...) { #name, type, { __VA_ARGS__ } },
#include "opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
};

} // namespace OpcodeInfo

Type GetTypeOf(Opcode op) {
    return OpcodeInfo::opcode_info.at(static_cast<size_t>(op)).type;
}

size_t GetNumArgsOf(Opcode op) {
    return OpcodeInfo::opcode_info.at(static_cast<size_t>(op)).arg_types.size();
}

Type GetArgTypeOf(Opcode op, size_t arg_index) {
    return OpcodeInfo::opcode_info.at(static_cast<size_t>(op)).arg_types.at(arg_index);
}

std::string GetNameOf(Opcode op) {
    if (OpcodeInfo::opcode_info.size() <= static_cast<size_t>(op))
        return fmt::format("Unknown Opcode {}", static_cast<Opcode>(op));
    return OpcodeInfo::opcode_info.at(static_cast<size_t>(op)).name;
}

std::ostream& operator<<(std::ostream& o, Opcode opcode) {
    return o << GetNameOf(opcode);
}

} // namespace Dynarmic::IR
