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

using T = Dynarmic::IR::Type;

struct Meta {
    const char* name;
    Type type;
    std::vector<Type> arg_types;
};

static const std::map<Opcode, Meta> opcode_info {{
#define OPCODE(name, type, ...) { Opcode::name, { #name, type, { __VA_ARGS__ } } },
#define A32OPC(name, type, ...) { Opcode::A32##name, { #name, type, { __VA_ARGS__ } } },
#define A64OPC(name, type, ...) { Opcode::A64##name, { #name, type, { __VA_ARGS__ } } },
#include "opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
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

std::string GetNameOf(Opcode op) {
    if (OpcodeInfo::opcode_info.count(op) == 0)
        return fmt::format("Unknown Opcode {}", static_cast<Opcode>(op));
    return OpcodeInfo::opcode_info.at(op).name;
}

std::ostream& operator<<(std::ostream& o, Opcode opcode) {
    return o << GetNameOf(opcode);
}

} // namespace Dynarmic::IR
