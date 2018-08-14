/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <cstring>

#include "backend/x64/block_of_code.h"
#include "backend/x64/constant_pool.h"
#include "common/assert.h"

namespace Dynarmic::BackendX64 {

ConstantPool::ConstantPool(BlockOfCode& code, size_t size) : code(code), pool_size(size) {
    code.int3();
    code.align(align_size);
    pool_begin = reinterpret_cast<u8*>(code.AllocateFromCodeSpace(size));
    current_pool_ptr = pool_begin;
}

Xbyak::Address ConstantPool::GetConstant(const Xbyak::AddressFrame& frame, u64 lower, u64 upper) {
    const auto constant = std::make_tuple(lower, upper);
    auto iter = constant_info.find(constant);
    if (iter == constant_info.end()) {
        ASSERT(static_cast<size_t>(current_pool_ptr - pool_begin) < pool_size);
        std::memcpy(current_pool_ptr, &lower, sizeof(u64));
        std::memcpy(current_pool_ptr + sizeof(u64), &upper, sizeof(u64));
        iter = constant_info.emplace(constant, current_pool_ptr).first;
        current_pool_ptr += align_size;
    }
    return frame[code.rip + iter->second];
}

} // namespace Dynarmic::BackendX64
