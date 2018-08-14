/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend/x64/callback.h"
#include "backend/x64/block_of_code.h"

namespace Dynarmic::BackendX64 {

Callback::~Callback() = default;

void SimpleCallback::EmitCall(BlockOfCode& code, std::function<void(RegList)> l) const {
    l({code.ABI_PARAM1, code.ABI_PARAM2, code.ABI_PARAM3, code.ABI_PARAM4});
    code.CallFunction(fn);
}

void SimpleCallback::EmitCallWithReturnPointer(BlockOfCode& code, std::function<void(Xbyak::Reg64, RegList)> l) const {
    l(code.ABI_PARAM1, {code.ABI_PARAM2, code.ABI_PARAM3, code.ABI_PARAM4});
    code.CallFunction(fn);
}

void ArgCallback::EmitCall(BlockOfCode& code, std::function<void(RegList)> l) const {
    l({code.ABI_PARAM2, code.ABI_PARAM3, code.ABI_PARAM4});
    code.mov(code.ABI_PARAM1, arg);
    code.CallFunction(fn);
}

void ArgCallback::EmitCallWithReturnPointer(BlockOfCode& code, std::function<void(Xbyak::Reg64, RegList)> l) const {
#if defined(WIN32) && !defined(__MINGW64__)
    l(code.ABI_PARAM2, {code.ABI_PARAM3, code.ABI_PARAM4});
    code.mov(code.ABI_PARAM1, arg);
#else
    l(code.ABI_PARAM1, {code.ABI_PARAM3, code.ABI_PARAM4});
    code.mov(code.ABI_PARAM2, arg);
#endif
    code.CallFunction(fn);
}

} // namespace Dynarmic::BackendX64
