/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>

#include <xbyak.h>

#include "common/common_types.h"

namespace Dynarmic::BackendX64 {

class BlockOfCode;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // Structure was padded due to alignment specifier
#endif

struct A32JitState {
    using ProgramCounterType = u32;

    A32JitState() { ResetRSB(); }

    std::array<u32, 16> Reg{}; // Current register file.
    // TODO: Mode-specific register sets unimplemented.

    u32 CPSR_et = 0;
    u32 CPSR_ge = 0;
    u32 CPSR_q = 0;
    u32 CPSR_nzcv = 0;
    u32 CPSR_jaifm = 0;

    u32 Cpsr() const;
    void SetCpsr(u32 cpsr);

    alignas(u64) std::array<u32, 64> ExtReg{}; // Extension registers.

    static constexpr size_t SpillCount = 64;
    std::array<u64, SpillCount> Spill{}; // Spill.
    static Xbyak::Address GetSpillLocationFromIndex(size_t i) {
        using namespace Xbyak::util;
        return qword[r15 + offsetof(A32JitState, Spill) + i * sizeof(u64)];
    }

    // For internal use (See: BlockOfCode::RunCode)
    u32 guest_MXCSR = 0x00001f80;
    u32 save_host_MXCSR = 0;
    s64 cycles_to_run = 0;
    s64 cycles_remaining = 0;
    bool halt_requested = false;

    // Exclusive state
    static constexpr u32 RESERVATION_GRANULE_MASK = 0xFFFFFFF8;
    u32 exclusive_state = 0;
    u32 exclusive_address = 0;

    static constexpr size_t RSBSize = 8; // MUST be a power of 2.
    static constexpr size_t RSBPtrMask = RSBSize - 1;
    u32 rsb_ptr = 0;
    std::array<u64, RSBSize> rsb_location_descriptors;
    std::array<u64, RSBSize> rsb_codeptrs;
    void ResetRSB();

    u32 fpsr_exc = 0;
    u32 fpsr_qc = 0; // Dummy value
    u32 FPSCR_IDC = 0;
    u32 FPSCR_UFC = 0;
    u32 FPSCR_mode = 0;
    u32 FPSCR_nzcv = 0;
    u32 old_FPSCR = 0;
    u32 Fpscr() const;
    void SetFpscr(u32 FPSCR);

    u64 GetUniqueHash() const;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

using CodePtr = const void*;

} // namespace Dynarmic::BackendX64
