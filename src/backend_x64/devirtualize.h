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

template <auto fn, typename F>
struct ThunkBuilder;

template <auto fn, typename C, typename R, typename... Args>
struct ThunkBuilder<fn, R(C::*)(Args...)> {
    static R Thunk(C* this_, Args... args) {
        return (this_->*fn)(std::forward<Args>(args)...);
    }
};

} // namespace impl

template <auto fn>
ArgCallback Devirtualize(mp::class_type_t<decltype(fn)>* this_) {
    return ArgCallback{&impl::ThunkBuilder<fn, decltype(fn)>::Thunk, reinterpret_cast<u64>(this_)};
}

} // namespace BackendX64
} // namespace Dynarmic
