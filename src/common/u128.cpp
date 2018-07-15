/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <array>

#include "common/common_types.h"
#include "common/u128.h"

namespace Dynarmic {

u128 operator<<(u128 operand, int amount) {
    if (amount < 0) {
        return operand >> -amount;
    }

    if (amount == 0) {
        return operand;
    }

    if (amount < 64) {
        u128 result;
        result.lower = (operand.lower << amount);
        result.upper = (operand.upper << amount) | (operand.lower >> (64 - amount));
        return result;
    }

    if (amount < 128) {
        u128 result;
        result.upper = operand.lower << (amount - 64);
        return result;
    }

    return {};
}

u128 operator>>(u128 operand, int amount) {
    if (amount < 0) {
        return operand << -amount;
    }

    if (amount == 0) {
        return operand;
    }

    if (amount < 64) {
        u128 result;
        result.lower = (operand.lower >> amount) | (operand.upper << (64 - amount));
        result.upper = (operand.upper >> amount);
        return result;
    }

    if (amount < 128) {
        u128 result;
        result.lower = operand.upper >> (amount - 64);
        return result;
    }

    return {};
}

} // namespace Dynarmic
