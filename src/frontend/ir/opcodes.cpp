/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>
#include <map>
#include <vector>

#include "frontend/ir/opcodes.h"

namespace Dynarmic {
namespace IR {

// Opcode information

namespace OpcodeInfo {

using T = Dynarmic::IR::Type;

struct Meta {
    const char* name;
    Type type;
    std::vector<Type> arg_types;
};

static const std::map<Opcode, Meta> opcode_info {{
#define OPCODE(name, type, ...) { Opcode::name, { #name, type, { __VA_ARGS__ } } },
#define A32OPC(name, type, ...) { Opcode::A32##name, { #name, type, { __VA_ARGS__ } } },
#include "opcodes.inc"
#undef OPCODE
#undef A32OPC
}};

} // namespace OpcodeInfo

Type GetTypeOf(Opcode op) {
    return OpcodeInfo::opcode_info.at(op).type;
}

size_t GetNumArgsOf(Opcode op) {
    return OpcodeInfo::opcode_info.at(op).arg_types.size();
}

Type GetArgTypeOf(Opcode op, size_t arg_index) {
    return OpcodeInfo::opcode_info.at(op).arg_types.at(arg_index);
}

const char* GetNameOf(Opcode op) {
    return OpcodeInfo::opcode_info.at(op).name;
}

const char* GetNameOf(Type type) {
    static const std::array<const char*, 14> names = {
        "Void", "A32Reg", "A32ExtReg", "A64Reg", "A64Vec", "Opaque", "U1", "U8", "U16", "U32", "U64", "F32", "F64", "CoprocInfo"
    };
    return names.at(static_cast<size_t>(type));
}

bool AreTypesCompatible(Type t1, Type t2) {
    return t1 == t2 || t1 == Type::Opaque || t2 == Type::Opaque;
}

} // namespace IR
} // namespace Dynarmic
