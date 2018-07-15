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

struct A64JitState {
    using ProgramCounterType = u64;

    A64JitState() { ResetRSB(); }

    std::array<u64, 31> reg{};
    u64 sp = 0;
    u64 pc = 0;

    u32 CPSR_nzcv = 0;

    u32 GetPstate() const {
        return CPSR_nzcv;
    }
    void SetPstate(u32 new_pstate) {
        CPSR_nzcv = new_pstate & 0xF0000000;
    }

    alignas(16) std::array<u64, 64> vec{}; // Extension registers.

    static constexpr size_t SpillCount = 64;
    alignas(16) std::array<std::array<u64, 2>, SpillCount> spill{}; // Spill.
    static Xbyak::Address GetSpillLocationFromIndex(size_t i) {
        using namespace Xbyak::util;
        return xword[r15 + offsetof(A64JitState, spill) + i * sizeof(u64) * 2];
    }

    // For internal use (See: BlockOfCode::RunCode)
    u32 guest_MXCSR = 0x00001f80;
    u32 save_host_MXCSR = 0;
    s64 cycles_to_run = 0;
    s64 cycles_remaining = 0;
    bool halt_requested = false;
    bool check_bit = false;

    // Exclusive state
    static constexpr u64 RESERVATION_GRANULE_MASK = 0xFFFF'FFFF'FFFF'FFF0ull;
    u8 exclusive_state = 0;
    u64 exclusive_address = 0;

    static constexpr size_t RSBSize = 8; // MUST be a power of 2.
    static constexpr size_t RSBPtrMask = RSBSize - 1;
    u32 rsb_ptr = 0;
    std::array<u64, RSBSize> rsb_location_descriptors;
    std::array<u64, RSBSize> rsb_codeptrs;
    void ResetRSB() {
        rsb_location_descriptors.fill(0xFFFFFFFFFFFFFFFFull);
        rsb_codeptrs.fill(0);
    }

    u32 fpsr_exc = 0;
    u32 fpsr_qc = 0;
    u32 FPSCR_IDC = 0;
    u32 FPSCR_UFC = 0;
    u32 fpcr = 0;
    u32 GetFpcr() const;
    u32 GetFpsr() const;
    void SetFpcr(u32 new_fpcr);
    void SetFpsr(u32 new_fpcr);

    u64 GetUniqueHash() const;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

using CodePtr = const void*;

} // namespace Dynarmic::BackendX64
