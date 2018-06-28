/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <boost/optional.hpp>

#include "common/bit_util.h"
#include "common/common_types.h"
#include "common/fp/rounding_mode.h"

namespace Dynarmic::A64 {

/**
 * Representation of the Floating-Point Control Register.
 */
class FPCR final
{
public:
    FPCR() = default;
    FPCR(const FPCR&) = default;
    FPCR(FPCR&&) = default;
    explicit FPCR(u32 data) : value{data & mask} {}

    FPCR& operator=(const FPCR&) = default;
    FPCR& operator=(FPCR&&) = default;
    FPCR& operator=(u32 data) {
        value = data & mask;
        return *this;
    }

    /// Alternate half-precision control flag.
    bool AHP() const {
        return Common::Bit<26>(value);
    }

    /// Alternate half-precision control flag.
    void AHP(bool AHP_) {
        value = Common::ModifyBit<26>(value, AHP_);
    }

    /// Default NaN mode control bit.
    bool DN() const {
        return Common::Bit<25>(value);
    }

    /// Flush-to-zero mode control bit.
    bool FZ() const {
        return Common::Bit<24>(value);
    }

    /// Rounding mode control field.
    FP::RoundingMode RMode() const {
        return static_cast<FP::RoundingMode>(Common::Bits<22, 23>(value));
    }

    bool FZ16() const {
        return Common::Bit<19>(value);
    }

    /// Input denormal exception trap enable flag.
    bool IDE() const {
        return Common::Bit<15>(value);
    }

    /// Inexact exception trap enable flag.
    bool IXE() const {
        return Common::Bit<12>(value);
    }

    /// Underflow exception trap enable flag.
    bool UFE() const {
        return Common::Bit<11>(value);
    }

    /// Overflow exception trap enable flag.
    bool OFE() const {
        return Common::Bit<10>(value);
    }

    /// Division by zero exception trap enable flag.
    bool DZE() const {
        return Common::Bit<9>(value);
    }

    /// Invalid operation exception trap enable flag.
    bool IOE() const {
        return Common::Bit<8>(value);
    }

    /// Gets the underlying raw value within the FPCR.
    u32 Value() const {
        return value;
    }

private:
    // Bits 0-7, 13-14, 19, and 27-31 are reserved.
    static constexpr u32 mask = 0x07F79F00;
    u32 value = 0;
};

inline bool operator==(FPCR lhs, FPCR rhs) {
    return lhs.Value() == rhs.Value();
}

inline bool operator!=(FPCR lhs, FPCR rhs) {
    return !operator==(lhs, rhs);
}

} // namespace Dynarmic::A64
