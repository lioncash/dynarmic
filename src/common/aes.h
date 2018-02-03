/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include "common/common_types.h"

namespace Dynarmic::Common {

using AESState = std::array<u8, 16>;

// Assumes the state has already been XORed by the round key.
void DecryptSingleRound(AESState& out_state, const AESState& state);
void EncryptSingleRound(AESState& out_state, const AESState& state);

void MixColumns(AESState& out_state, const AESState& state);
void InverseMixColumns(AESState& out_state, const AESState& state);

} // namespace Dynarmic::Common
