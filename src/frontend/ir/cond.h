/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/common_types.h"

namespace Dynarmic {
namespace IR {

enum class Cond {
    EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL, NV,
    HS = CS, LO = CC,
};

inline Cond invert(Cond c) {
	return static_cast<Cond>(static_cast<size_t>(c) ^ 1);
}

} // namespace IR
} // namespace Dynarmic
