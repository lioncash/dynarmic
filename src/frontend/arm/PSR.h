/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include "common/bit_util.h"
#include "common/common_types.h"

namespace Dynarmic {
namespace Arm {

/**
 * Program Status Register
 *
 * | Bit(s)  | Description                                   |
 * |:-------:|:----------------------------------------------|
 * | N       | Negative                                      |
 * | Z       | Zero                                          |
 * | C       | Carry                                         |
 * | V       | Overflow                                      |
 * | Q       | Sticky overflow for DSP-oriented instructions |
 * | IT[1:0] | Lower two bits of the If-Then execution state |
 * | J       | Jazelle bit                                   |
 * | GE      | Greater-than or Equal                         |
 * | IT[7:2] | Upper six bits of the If-Then execution state |
 * | E       | Endian (0 is little endian, 1 is big endian)  |
 * | A       | Imprecise data abort (disables them when set) |
 * | I       | IRQ interrupts (disabled when set)            |
 * | F       | FIQ interrupts (disabled when set)            |
 * | T       | Thumb bit                                     |
 * | M       | Current processor mode                        |
 */
class PSR final {
public:
    /// Valid processor modes that may be indicated.
    enum class Mode : u32
    {
        User       = 0b10000,
        FIQ        = 0b10001,
        IRQ        = 0b10010,
        Supervisor = 0b10011,
        Monitor    = 0b10110,
        Abort      = 0b10111,
        Hypervisor = 0b11010,
        Undefined  = 0b11011,
        System     = 0b11111
    };

    /// Instruction sets that may be signified through a PSR.
    enum class InstructionSet
    {
        ARM,
        Jazelle,
        Thumb,
        ThumbEE
    };

    PSR() = default;
    explicit PSR(u32 data) : value{data & mask} {}

    PSR& operator=(u32 data) {
        value = data & mask;
        return *this;
    }

    bool N() const {
        return Common::Bit<31>(value);
    }
    void N(bool set) {
        value = (value & ~0x80000000) | static_cast<u32>(set) << 31;
    }

    bool Z() const {
        return Common::Bit<30>(value);
    }
    void Z(bool set) {
        value = (value & ~0x40000000) | static_cast<u32>(set) << 30;
    }

    bool C() const {
        return Common::Bit<29>(value);
    }
    void C(bool set) {
        value = (value & ~0x20000000) | static_cast<u32>(set) << 29;
    }

    bool V() const {
        return Common::Bit<28>(value);
    }
    void V(bool set) {
        value = (value & ~0x10000000) | static_cast<u32>(set) << 28;
    }

    bool Q() const {
        return Common::Bit<27>(value);
    }
    void Q(bool set) {
        value = (value & ~0x8000000) | static_cast<u32>(set) << 27;
    }

    bool J() const {
        return Common::Bit<24>(value);
    }
    void J(bool set) {
        value = (value & ~0x1000000) | static_cast<u32>(set) << 24;
    }

    u32 GE() const {
        return Common::Bits<16, 19>(value);
    }
    void GE(u32 data) {
        value = (value & ~0xF0000) | (data & 0xF) << 16;
    }

    u32 IT() const {
        return (value & 0x6000000) >> 25 | (value & 0xFC00) >> 8;
    }
    void IT(u32 data) {
        value = (value & ~0x000FC00) | (data & 0b11111100) << 8;
        value = (value & ~0x6000000) | (data & 0b00000011) << 25;
    }

    bool E() const {
        return Common::Bit<9>(value);
    }
    void E(bool set) {
        value = (value & ~0x200) | static_cast<u32>(set) << 9;
    }

    bool A() const {
        return Common::Bit<8>(value);
    }
    void A(bool set) {
        value = (value & ~0x100) | static_cast<u32>(set) << 8;
    }

    bool I() const {
        return Common::Bit<7>(value);
    }
    void I(bool set) {
        value = (value & ~0x80) | static_cast<u32>(set) << 7;
    }

    bool F() const {
        return Common::Bit<6>(value);
    }
    void F(bool set) {
        value = (value & ~0x40) | static_cast<u32>(set) << 6;
    }

    bool T() const {
        return Common::Bit<5>(value);
    }
    void T(bool set) {
        value = (value & ~0x20) | static_cast<u32>(set) << 5;
    }

    Mode M() const {
        return static_cast<Mode>(Common::Bits<0, 4>(value));
    }
    void M(Mode mode) {
        value = (value & ~0x1F) | (static_cast<u32>(mode) & 0x1F);
    }

    u32 Value() const {
        return value;
    }

    InstructionSet CurrentInstructionSet() const {
        const bool j_bit = J();
        const bool t_bit = T();

        if (j_bit && t_bit)
            return InstructionSet::ThumbEE;

        if (t_bit)
            return InstructionSet::Thumb;

        if (j_bit)
            return InstructionSet::Jazelle;

        return InstructionSet::ARM;
    }

    void CurrentInstructionSet(InstructionSet instruction_set) {
        switch (instruction_set) {
        case InstructionSet::ARM:
            T(false);
            J(false);
            break;
        case InstructionSet::Jazelle:
            T(false);
            J(true);
            break;
        case InstructionSet::Thumb:
            T(true);
            J(false);
            break;
        case InstructionSet::ThumbEE:
            T(true);
            J(true);
            break;
        }
    }

private:
    // Bits 20-23 are reserved and should be zero.
    static constexpr u32 mask = 0xFF0FFFFF;

    u32 value = 0;
};

inline bool operator==(PSR lhs, PSR rhs) {
    return lhs.Value() == rhs.Value();
}

inline bool operator!=(PSR lhs, PSR rhs) {
    return !operator==(lhs, rhs);
}

} // namespace Arm
} // namespace Dynarmic
