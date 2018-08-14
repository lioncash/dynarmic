/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <unordered_set>

#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>

#include "backend/x64/block_range_information.h"
#include "common/common_types.h"

namespace Dynarmic::BackendX64 {

template <typename ProgramCounterType>
void BlockRangeInformation<ProgramCounterType>::AddRange(boost::icl::discrete_interval<ProgramCounterType> range, IR::LocationDescriptor location) {
    block_ranges.add(std::make_pair(range, std::set<IR::LocationDescriptor>{location}));
}

template <typename ProgramCounterType>
void BlockRangeInformation<ProgramCounterType>::ClearCache() {
    block_ranges.clear();
}

template <typename ProgramCounterType>
std::unordered_set<IR::LocationDescriptor> BlockRangeInformation<ProgramCounterType>::InvalidateRanges(const boost::icl::interval_set<ProgramCounterType>& ranges) {
    std::unordered_set<IR::LocationDescriptor> erase_locations;
    for (auto invalidate_interval : ranges) {
        auto pair = block_ranges.equal_range(invalidate_interval);
        for (auto it = pair.first; it != pair.second; ++it) {
            for (const auto &descriptor : it->second) {
                erase_locations.insert(descriptor);
            }
        }
    }
    // TODO: EFFICIENCY: Remove ranges that are to be erased.
    return erase_locations;
}

template class BlockRangeInformation<u32>;
template class BlockRangeInformation<u64>;

} // namespace Dynarmic::BackendX64
