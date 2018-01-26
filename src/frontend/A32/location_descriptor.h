/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <functional>
#include <iosfwd>
#include <tuple>
#include "common/common_types.h"
#include "frontend/A32/FPSCR.h"
#include "frontend/A32/PSR.h"
#include "frontend/ir/location_descriptor.h"

namespace Dynarmic::A32 {

/**
 * LocationDescriptor describes the location of a basic block.
 * The location is not solely based on the PC because other flags influence the way
 * instructions should be translated. The CPSR.T flag is most notable since it
 * tells us if the processor is in Thumb or Arm mode.
 */
class LocationDescriptor {
public:
    // Indicates bits that should be preserved within descriptors.
    static constexpr u32 CPSR_MODE_MASK  = 0x00000220;
    static constexpr u32 FPSCR_MODE_MASK = 0x03F79F00;

    LocationDescriptor(u32 arm_pc, PSR cpsr, FPSCR fpscr)
            : arm_pc(arm_pc), cpsr(cpsr.Value() & CPSR_MODE_MASK), fpscr(fpscr.Value() & FPSCR_MODE_MASK) {}

    explicit LocationDescriptor(const IR::LocationDescriptor& o) {
        arm_pc = o.Value() >> 32;
        cpsr.T(o.Value() & 1);
        cpsr.E(o.Value() & 2);
        fpscr = o.Value() & FPSCR_MODE_MASK;
    }

    u32 PC() const { return arm_pc; }
    bool TFlag() const { return cpsr.T(); }
    bool EFlag() const { return cpsr.E(); }

    A32::PSR CPSR() const { return cpsr; }
    A32::FPSCR FPSCR() const { return fpscr; }

    bool operator == (const LocationDescriptor& o) const {
        return std::tie(arm_pc, cpsr, fpscr) == std::tie(o.arm_pc, o.cpsr, o.fpscr);
    }

    bool operator != (const LocationDescriptor& o) const {
        return !operator==(o);
    }

    LocationDescriptor SetPC(u32 new_arm_pc) const {
        return LocationDescriptor(new_arm_pc, cpsr, fpscr);
    }

    LocationDescriptor AdvancePC(int amount) const {
        return LocationDescriptor(static_cast<u32>(arm_pc + amount), cpsr, fpscr);
    }

    LocationDescriptor SetTFlag(bool new_tflag) const {
        PSR new_cpsr = cpsr;
        new_cpsr.T(new_tflag);

        return LocationDescriptor(arm_pc, new_cpsr, fpscr);
    }

    LocationDescriptor SetEFlag(bool new_eflag) const {
        PSR new_cpsr = cpsr;
        new_cpsr.E(new_eflag);

        return LocationDescriptor(arm_pc, new_cpsr, fpscr);
    }

    LocationDescriptor SetFPSCR(u32 new_fpscr) const {
        return LocationDescriptor(arm_pc, cpsr, A32::FPSCR{new_fpscr & FPSCR_MODE_MASK});
    }

    u64 UniqueHash() const {
        // This value MUST BE UNIQUE.
        // This calculation has to match up with EmitX64::EmitTerminalPopRSBHint
        u64 pc_u64 = u64(arm_pc) << 32;
        u64 fpscr_u64 = u64(fpscr.Value());
        u64 t_u64 = cpsr.T() ? 1 : 0;
        u64 e_u64 = cpsr.E() ? 2 : 0;
        return pc_u64 | fpscr_u64 | t_u64 | e_u64;
    }

    operator IR::LocationDescriptor() const {
        return IR::LocationDescriptor{UniqueHash()};
    }

private:
    u32 arm_pc;       ///< Current program counter value.
    PSR cpsr;         ///< Current program status register.
    A32::FPSCR fpscr; ///< Floating point status control register.
};

/**
 * Provides a string representation of a LocationDescriptor.
 *
 * @param o          Output stream
 * @param descriptor The descriptor to get a string representation of
 */
std::ostream& operator<<(std::ostream& o, const LocationDescriptor& descriptor);

} // namespace Dynarmic::A32

namespace std {
template <>
struct less<Dynarmic::A32::LocationDescriptor> {
    bool operator()(const Dynarmic::A32::LocationDescriptor& x, const Dynarmic::A32::LocationDescriptor& y) const {
        return x.UniqueHash() < y.UniqueHash();
    }
};
template <>
struct hash<Dynarmic::A32::LocationDescriptor> {
    size_t operator()(const Dynarmic::A32::LocationDescriptor& x) const {
        return std::hash<u64>()(x.UniqueHash());
    }
};
} // namespace std
