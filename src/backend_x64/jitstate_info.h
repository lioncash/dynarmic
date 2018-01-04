/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <cstddef>

#include "common/common_types.h"

namespace Dynarmic {
namespace BackendX64 {

struct JitStateInfo {
    template <typename JitStateType>
    JitStateInfo(const JitStateType&)
        : offsetof_cycles_remaining(offsetof(JitStateType, cycles_remaining))
        , offsetof_cycles_to_run(offsetof(JitStateType, cycles_to_run))
        , offsetof_save_host_MXCSR(offsetof(JitStateType, save_host_MXCSR))
        , offsetof_guest_MXCSR(offsetof(JitStateType, guest_MXCSR))
    {}

    const size_t offsetof_cycles_remaining;
    const size_t offsetof_cycles_to_run;
    const size_t offsetof_save_host_MXCSR;
    const size_t offsetof_guest_MXCSR;
};

} // namespace BackendX64
} // namespace Dynarmic
