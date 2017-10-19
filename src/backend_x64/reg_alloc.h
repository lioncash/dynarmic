/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <forward_list>

#include <boost/optional.hpp>
#include <xbyak.h>

#include "backend_x64/block_of_code.h"
#include "backend_x64/hostloc.h"
#include "backend_x64/oparg.h"
#include "common/common_types.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/value.h"

namespace Dynarmic {
namespace BackendX64 {

class RegAlloc;

struct HostLocInfo {
public:
    bool IsLocked() const;
    bool IsEmpty() const;
    bool IsLastUse() const;

    bool ContainsValue(const IR::Inst* inst) const;

    void ReadLock();
    void WriteLock();

    void AddValue(IR::Inst* inst);

    void EndOfAllocScope();

private:
    std::forward_list<IR::Inst*> values;
    bool is_being_used = false;
    bool is_scratch = false;
};

struct Argument {
public:
    IR::Type GetType() const;
    bool IsImmediate() const;

    bool GetImmediateU1() const;
    u8 GetImmediateU8() const;
    u16 GetImmediateU16() const;
    u32 GetImmediateU32() const;
    u64 GetImmediateU64() const;

    /// Is this value currently in a GPR?
    bool IsInGpr() const;
    /// Is this value currently in a XMM?
    bool IsInXmm() const;
    /// Is this value currently in memory?
    bool IsInMemory() const;

private:
    friend class RegAlloc;
    Argument(RegAlloc& reg_alloc) : reg_alloc(reg_alloc) {}

    bool allocated = false;
    RegAlloc& reg_alloc;
    IR::Value value;
};

class RegAlloc final {
public:
    explicit RegAlloc(BlockOfCode* code) : code(code) {}

    std::array<Argument, 3> GetArgumentInfo(IR::Inst* inst);

    Xbyak::Reg64 UseGpr(Argument& arg);
    Xbyak::Xmm UseXmm(Argument& arg);
    OpArg UseOpArg(Argument& arg);
    void Use(Argument& arg, HostLoc host_loc);

    Xbyak::Reg64 UseScratchGpr(Argument& arg);
    Xbyak::Xmm UseScratchXmm(Argument& arg);
    void UseScratch(Argument& arg, HostLoc host_loc);

    void DefineValue(IR::Inst* inst, const Xbyak::Reg& reg);
    void DefineValue(IR::Inst* inst, Argument& arg);

    Xbyak::Reg64 ScratchGpr(HostLocList desired_locations = any_gpr);
    Xbyak::Xmm ScratchXmm(HostLocList desired_locations = any_xmm);

    void HostCall(IR::Inst* result_def = nullptr, boost::optional<Argument&> arg0 = {}, boost::optional<Argument&> arg1 = {}, boost::optional<Argument&> arg2 = {}, boost::optional<Argument&> arg3 = {});

    // TODO: Values in host flags

    void EndOfAllocScope();

    void AssertNoMoreUses();

private:
    friend struct Argument;

    HostLoc SelectARegister(HostLocList desired_locations) const;
    boost::optional<HostLoc> ValueLocation(const IR::Inst* value) const;

    HostLoc UseImpl(IR::Value use_value, HostLocList desired_locations);
    HostLoc UseScratchImpl(IR::Value use_value, HostLocList desired_locations);
    HostLoc ScratchImpl(HostLocList desired_locations);
    void DefineValueImpl(IR::Inst* def_inst, HostLoc host_loc);
    void DefineValueImpl(IR::Inst* def_inst, const IR::Value& use_inst);

    BlockOfCode* code = nullptr;

    HostLoc LoadImmediate(IR::Value imm, HostLoc reg);
    void Move(HostLoc to, HostLoc from);
    void CopyToScratch(HostLoc to, HostLoc from);
    void Exchange(HostLoc a, HostLoc b);
    void MoveOutOfTheWay(HostLoc reg);

    void SpillRegister(HostLoc loc);
    HostLoc FindFreeSpill() const;

    std::array<HostLocInfo, HostLocCount> hostloc_info;
    HostLocInfo& LocInfo(HostLoc loc);
    const HostLocInfo& LocInfo(HostLoc loc) const;
};

} // namespace BackendX64
} // namespace Dynarmic
