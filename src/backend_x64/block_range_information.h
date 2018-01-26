/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <unordered_set>

#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>

#include "frontend/ir/location_descriptor.h"

namespace Dynarmic::BackendX64 {

template <typename ProgramCounterType>
class BlockRangeInformation {
public:
    void AddRange(boost::icl::discrete_interval<ProgramCounterType> range, IR::LocationDescriptor location);
    void ClearCache();
    std::unordered_set<IR::LocationDescriptor> InvalidateRanges(const boost::icl::interval_set<ProgramCounterType>& ranges);

private:
    boost::icl::interval_map<ProgramCounterType, std::set<IR::LocationDescriptor>> block_ranges;
};

} // namespace Dynarmic::BackendX64
