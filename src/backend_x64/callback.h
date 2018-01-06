/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <functional>

#include <xbyak.h>

#include "common/common_types.h"

namespace Dynarmic {
namespace BackendX64 {

class BlockOfCode;

class Callback {
public:
    virtual ~Callback() = default;

    virtual void EmitCall(BlockOfCode* code, std::function<void()> fn = []{}) = 0;
    virtual void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64)> fn) = 0;
    virtual void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64)> fn) = 0;
    virtual void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64, Xbyak::Reg64)> fn) = 0;
};

class SimpleCallback final : public Callback {
public:
    template <typename Function>
    SimpleCallback(Function fn) : fn(reinterpret_cast<void(*)()>(fn)) {}

    ~SimpleCallback() = default;

    void EmitCall(BlockOfCode* code, std::function<void()> l = []{}) override;
    void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64)> l) override;
    void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64)> l) override;
    void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64, Xbyak::Reg64)> l) override;

private:
    void (*fn)();
};

class ArgCallback final : public Callback {
public:
    template <typename Function>
    ArgCallback(Function fn, u64 arg) : fn(reinterpret_cast<void(*)()>(fn)), arg(arg) {}

    ~ArgCallback() = default;

    void EmitCall(BlockOfCode* code, std::function<void()> l = []{}) override;
    void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64)> l) override;
    void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64)> l) override;
    void EmitCall(BlockOfCode* code, std::function<void(Xbyak::Reg64, Xbyak::Reg64, Xbyak::Reg64)> l) override;

private:
    void (*fn)();
    u64 arg;
};

} // namespace BackendX64
} // namespace Dynarmic
