/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <initializer_list>

#include "common/common_types.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/types.h"
#include "frontend/ir/ir_emitter.h"
#include "frontend/ir/value.h"

namespace Dynarmic {
namespace A64 {

/**
 * Convenience class to construct a basic block of the intermediate representation.
 * `block` is the resulting block.
 * The user of this class updates `current_location` as appropriate.
 */
class IREmitter : public IR::IREmitter {
public:
    explicit IREmitter(LocationDescriptor descriptor) : IR::IREmitter(descriptor), current_location(descriptor) {}

    LocationDescriptor current_location;

    u64 PC();
    u64 AlignPC(size_t alignment);

    IR::U1 GetCFlag();
    void SetNZCV(const IR::NZCV& nzcv);

    IR::U32 GetW(Reg source_reg);
    IR::U64 GetX(Reg source_reg);
    IR::U64 GetSP();
    void SetW(Reg dest_reg, const IR::U32& value);
    void SetX(Reg dest_reg, const IR::U64& value);
    void SetSP(const IR::U64& value);
};

} // namespace IR
} // namespace Dynarmic
