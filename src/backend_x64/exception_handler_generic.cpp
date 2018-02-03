/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend_x64/block_of_code.h"

namespace Dynarmic::BackendX64 {

struct BlockOfCode::ExceptionHandler::Impl final {
};

BlockOfCode::ExceptionHandler::ExceptionHandler() = default;
BlockOfCode::ExceptionHandler::~ExceptionHandler() = default;

void BlockOfCode::ExceptionHandler::Register(BlockOfCode&) {
    // Do nothing
}

} // namespace Dynarmic::BackendX64
