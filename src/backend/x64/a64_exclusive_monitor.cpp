/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>

#include <dynarmic/A64/exclusive_monitor.h>
#include "common/assert.h"

namespace Dynarmic {
namespace A64 {

ExclusiveMonitor::ExclusiveMonitor(size_t processor_count) : exclusive_addresses(processor_count, INVALID_EXCLUSIVE_ADDRESS) {
    Unlock();
}

size_t ExclusiveMonitor::GetProcessorCount() const {
    return exclusive_addresses.size();
}

void ExclusiveMonitor::Mark(size_t processor_id, VAddr address, size_t size) {
    ASSERT(size <= 16);
    const VAddr masked_address = address & RESERVATION_GRANULE_MASK;

    Lock();
    exclusive_addresses[processor_id] = masked_address;
    Unlock();
}

void ExclusiveMonitor::Lock() {
    while (is_locked.test_and_set()) {}
}

void ExclusiveMonitor::Unlock() {
    is_locked.clear();
}

bool ExclusiveMonitor::CheckAndClear(size_t processor_id, VAddr address, size_t size) {
    ASSERT(size <= 16);
    const VAddr masked_address = address & RESERVATION_GRANULE_MASK;

    Lock();
    if (exclusive_addresses[processor_id] != masked_address) {
        Unlock();
        return false;
    }

    for (VAddr& other_address : exclusive_addresses) {
        if (other_address == masked_address) {
            other_address = INVALID_EXCLUSIVE_ADDRESS;
        }
    }
    return true;
}

void ExclusiveMonitor::Clear() {
    Lock();
    std::fill(exclusive_addresses.begin(), exclusive_addresses.end(), INVALID_EXCLUSIVE_ADDRESS);
    Unlock();
}

} // namespace A64
} // namespace Dynarmic
