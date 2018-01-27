/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <memory>

#include "backend_x64/callback.h"
#include "common/common_types.h"
#include "common/mp.h"

namespace Dynarmic {
namespace BackendX64 {

namespace impl {

template <typename FunctionType, FunctionType mfp>
struct ThunkBuilder;

template <typename C, typename R, typename... Args, R(C::*mfp)(Args...)>
struct ThunkBuilder<R(C::*)(Args...), mfp> {
    static R Thunk(C* this_, Args... args) {
        return (this_->*mfp)(std::forward<Args>(args)...);
    }
};

} // namespace impl

template <typename FunctionType, FunctionType mfp>
ArgCallback Devirtualize(mp::class_type_t<FunctionType>* this_) {
    return ArgCallback{&impl::ThunkBuilder<FunctionType, mfp>::Thunk, reinterpret_cast<u64>(this_)};
}

#define DEVIRT(this_, mfp) Dynarmic::BackendX64::Devirtualize<decltype(mfp), mfp>(this_)

} // namespace BackendX64
} // namespace Dynarmic
