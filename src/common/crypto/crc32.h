/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/common_types.h"

namespace Dynarmic::Common::Crypto::CRC32 {

/**
 * Computes a CRC32 value using Castagnoli polynomial (0x1EDC6F41).
 *
 * @param crc The initial CRC value
 * @param value The value to compute the CRC of.
 * @param length The length of the data to compute.
 *
 * @remark @p value is interpreted internally as an array of
 *         unsigned char with @p length data.
 *
 * @return The computed CRC32 value.
 */
u32 ComputeCRC32Castagnoli(u32 crc, u64 value, int length);

/**
 * Computes a CRC32 value using the ISO polynomial (0x04C11DB7).
 *
 * @param crc The initial CRC value
 * @param value The value to compute the CRC of.
 * @param length The length of the data to compute.
 *
 * @remark @p value is interpreted internally as an array of
 *         unsigned char with @p length data.
 *
 * @return The computed CRC32 value.
 */
u32 ComputeCRC32ISO(u32 crc, u64 value, int length);

} // namespace Dynarmic::Common::Crypto::CRC32
