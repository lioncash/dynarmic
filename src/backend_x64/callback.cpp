/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "backend_x64/block_of_code.h"
#include "backend_x64/callback.h"

namespace Dynarmic {
namespace BackendX64 {

void SimpleCallback::EmitCall(BlockOfCode* code, std::function<void()> l) {
    l();
    code->CallFunction(fn);
}

void SimpleCallback::EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64)> l) {
    l(code->ABI_PARAM1);
    code->CallFunction(fn);
}

void SimpleCallback::EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64)> l) {
    l(code->ABI_PARAM1, code->ABI_PARAM2);
    code->CallFunction(fn);
}

void SimpleCallback::EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64, Xbyak::Reg64)> l) {
    l(code->ABI_PARAM1, code->ABI_PARAM2, code->ABI_PARAM3);
    code->CallFunction(fn);
}

void ArgCallback::EmitCall(BlockOfCode* code, std::function<void()> l) {
    l();
    code->mov(code->ABI_PARAM1, arg);
    code->CallFunction(fn);
}

void ArgCallback::EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64)> l) {
    l(code->ABI_PARAM2);
    code->mov(code->ABI_PARAM1, arg);
    code->CallFunction(fn);
}

void ArgCallback::EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64)> l) {
    l(code->ABI_PARAM2, code->ABI_PARAM3);
    code->mov(code->ABI_PARAM1, arg);
    code->CallFunction(fn);
}

void ArgCallback::EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64, Xbyak::Reg64)> l) {
    l(code->ABI_PARAM2, code->ABI_PARAM3, code->ABI_PARAM4);
    code->mov(code->ABI_PARAM1, arg);
    code->CallFunction(fn);
}

} // namespace BackendX64
} // namespace Dynarmic
