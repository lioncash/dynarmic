/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <string>

namespace Dynarmic::BackendX64 {

std::string DisassembleX64(const void* pos, const void* end);

} // namespace Dynarmic::BackendX64
