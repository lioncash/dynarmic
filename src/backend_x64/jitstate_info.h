/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <cstddef>

#include "common/common_types.h"

namespace Dynarmic::BackendX64 {

struct JitStateInfo {
    template <typename JitStateType>
    JitStateInfo(const JitStateType&)
        : offsetof_cycles_remaining(offsetof(JitStateType, cycles_remaining))
        , offsetof_cycles_to_run(offsetof(JitStateType, cycles_to_run))
        , offsetof_save_host_MXCSR(offsetof(JitStateType, save_host_MXCSR))
        , offsetof_guest_MXCSR(offsetof(JitStateType, guest_MXCSR))
        , offsetof_rsb_ptr(offsetof(JitStateType, rsb_ptr))
        , rsb_ptr_mask(JitStateType::RSBPtrMask)
        , offsetof_rsb_location_descriptors(offsetof(JitStateType, rsb_location_descriptors))
        , offsetof_rsb_codeptrs(offsetof(JitStateType, rsb_codeptrs))
        , offsetof_CPSR_nzcv(offsetof(JitStateType, CPSR_nzcv))
        , offsetof_FPSCR_IDC(offsetof(JitStateType, FPSCR_IDC))
        , offsetof_FPSCR_UFC(offsetof(JitStateType, FPSCR_UFC))
        , offsetof_fpsr_exc(offsetof(JitStateType, fpsr_exc))
        , offsetof_fpsr_qc(offsetof(JitStateType, fpsr_qc))
    {}

    const size_t offsetof_cycles_remaining;
    const size_t offsetof_cycles_to_run;
    const size_t offsetof_save_host_MXCSR;
    const size_t offsetof_guest_MXCSR;
    const size_t offsetof_rsb_ptr;
    const size_t rsb_ptr_mask;
    const size_t offsetof_rsb_location_descriptors;
    const size_t offsetof_rsb_codeptrs;
    const size_t offsetof_CPSR_nzcv;
    const size_t offsetof_FPSCR_IDC;
    const size_t offsetof_FPSCR_UFC;
    const size_t offsetof_fpsr_exc;
    const size_t offsetof_fpsr_qc;
};

} // namespace Dynarmic::BackendX64
