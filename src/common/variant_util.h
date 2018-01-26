/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <boost/variant.hpp>

namespace Dynarmic::Common {
namespace detail {

template <typename ReturnT, typename Lambda>
struct VariantVisitor : boost::static_visitor<ReturnT>, Lambda {
   VariantVisitor(Lambda&& lambda)
       : Lambda(std::move(lambda))
   {}

   using Lambda::operator();
};

} // namespace detail

template<typename ReturnT, typename Variant, typename Lambda>
inline ReturnT VisitVariant(Variant&& variant, Lambda&& lambda) {
    return boost::apply_visitor(detail::VariantVisitor<ReturnT, Lambda>(std::move(lambda)), variant);
}

} // namespace Dynarmic::Common
