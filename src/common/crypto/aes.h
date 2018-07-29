/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include "common/common_types.h"

namespace Dynarmic::Common::Crypto::AES {

using State = std::array<u8, 16>;

// Assumes the state has already been XORed by the round key.
void DecryptSingleRound(State& out_state, const State& state);
void EncryptSingleRound(State& out_state, const State& state);

void MixColumns(State& out_state, const State& state);
void InverseMixColumns(State& out_state, const State& state);

} // namespace Dynarmic::Common::Crypto::AES
