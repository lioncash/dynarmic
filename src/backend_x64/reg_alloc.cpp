/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>
#include <vector>

#include <xbyak.h>

#include "backend_x64/abi.h"
#include "backend_x64/jitstate.h"
#include "backend_x64/reg_alloc.h"
#include "common/assert.h"

namespace Dynarmic {
namespace BackendX64 {

static u64 ImmediateToU64(const IR::Value& imm) {
    switch (imm.GetType()) {
    case IR::Type::U1:
        return u64(imm.GetU1());
    case IR::Type::U8:
        return u64(imm.GetU8());
    case IR::Type::U16:
        return u64(imm.GetU16());
    case IR::Type::U32:
        return u64(imm.GetU32());
    case IR::Type::U64:
        return u64(imm.GetU64());
    default:
        ASSERT_MSG(false, "This should never happen.");
    }
}

static bool IsSameHostLocClass(HostLoc a, HostLoc b) {
    return (HostLocIsGPR(a) && HostLocIsGPR(b))
           || (HostLocIsXMM(a) && HostLocIsXMM(b))
           || (HostLocIsSpill(a) && HostLocIsSpill(b));
}

static void EmitMove(BlockOfCode* code, HostLoc to, HostLoc from) {
    if (HostLocIsXMM(to) && HostLocIsXMM(from)) {
        code->movaps(HostLocToXmm(to), HostLocToXmm(from));
    } else if (HostLocIsGPR(to) && HostLocIsGPR(from)) {
        code->mov(HostLocToReg64(to), HostLocToReg64(from));
    } else if (HostLocIsXMM(to) && HostLocIsGPR(from)) {
        code->movq(HostLocToXmm(to), HostLocToReg64(from));
    } else if (HostLocIsGPR(to) && HostLocIsXMM(from)) {
        code->movq(HostLocToReg64(to), HostLocToXmm(from));
    } else if (HostLocIsXMM(to) && HostLocIsSpill(from)) {
        code->movsd(HostLocToXmm(to), SpillToOpArg(from));
    } else if (HostLocIsSpill(to) && HostLocIsXMM(from)) {
        code->movsd(SpillToOpArg(to), HostLocToXmm(from));
    } else if (HostLocIsGPR(to) && HostLocIsSpill(from)) {
        code->mov(HostLocToReg64(to), SpillToOpArg(from));
    } else if (HostLocIsSpill(to) && HostLocIsGPR(from)) {
        code->mov(SpillToOpArg(to), HostLocToReg64(from));
    } else {
        ASSERT_MSG(false, "Invalid RegAlloc::EmitMove");
    }
}

static void EmitExchange(BlockOfCode* code, HostLoc a, HostLoc b) {
    if (HostLocIsGPR(a) && HostLocIsGPR(b)) {
        code->xchg(HostLocToReg64(a), HostLocToReg64(b));
    } else if (HostLocIsXMM(a) && HostLocIsXMM(b)) {
        ASSERT_MSG(false, "Check your code: Exchanging XMM registers is unnecessary");
    } else {
        ASSERT_MSG(false, "Invalid RegAlloc::EmitExchange");
    }
}

bool HostLocInfo::IsLocked() const {
    return is_being_used;
}

bool HostLocInfo::IsEmpty() const {
    return !is_being_used && values.empty();
}

bool HostLocInfo::IsLastUse() const {
    return !is_being_used && std::all_of(values.begin(), values.end(), [](const auto& inst) { return !inst->HasUses(); });
}

bool HostLocInfo::ContainsValue(const IR::Inst* inst) const {
    return std::find(values.begin(), values.end(), inst) != values.end();
}

void HostLocInfo::ReadLock() {
    ASSERT(!is_scratch);
    is_being_used = true;
}

void HostLocInfo::WriteLock() {
    ASSERT(!is_being_used);
    is_being_used = true;
    is_scratch = true;
}

void HostLocInfo::AddValue(IR::Inst* inst) {
    values.push_front(inst);
}

void HostLocInfo::EndOfAllocScope() {
    values.remove_if([](const auto& inst) { return !inst->HasUses(); });

    is_being_used = false;
    is_scratch = false;
}

IR::Type Argument::GetType() const {
    return value.GetType();
}

bool Argument::IsImmediate() const {
    return value.IsImmediate();
}

bool Argument::GetImmediateU1() const {
    return value.GetU1();
}

u8 Argument::GetImmediateU8() const {
    u64 imm = ImmediateToU64(value);
    ASSERT(imm < 0x100);
    return u8(imm);
}

u16 Argument::GetImmediateU16() const {
    u64 imm = ImmediateToU64(value);
    ASSERT(imm < 0x10000);
    return u16(imm);
}

u32 Argument::GetImmediateU32() const {
    u64 imm = ImmediateToU64(value);
    ASSERT(imm < 0x100000000);
    return u32(imm);
}

u64 Argument::GetImmediateU64() const {
    return ImmediateToU64(value);
}

bool Argument::IsInGpr() const {
    return HostLocIsGPR(*reg_alloc.ValueLocation(value.GetInst()));
}

bool Argument::IsInXmm() const {
    return HostLocIsXMM(*reg_alloc.ValueLocation(value.GetInst()));
}

bool Argument::IsInMemory() const {
    return HostLocIsSpill(*reg_alloc.ValueLocation(value.GetInst()));
}

std::array<Argument, 3> RegAlloc::GetArgumentInfo(IR::Inst* inst) {
    std::array<Argument, 3> ret = { Argument{*this}, Argument{*this}, Argument{*this} };
    for (size_t i = 0; i < inst->NumArgs(); i++) {
        IR::Value arg = inst->GetArg(i);
        ret[i].value = arg;
        if (!arg.IsImmediate()) {
            arg.GetInst()->DecrementRemainingUses();
        }
    }
    return ret;
}

Xbyak::Reg64 RegAlloc::UseGpr(Argument& arg) {
    ASSERT(!arg.allocated);
    arg.allocated = true;
    return HostLocToReg64(UseImpl(arg.value, any_gpr));
}

Xbyak::Xmm RegAlloc::UseXmm(Argument& arg) {
    ASSERT(!arg.allocated);
    arg.allocated = true;
    return HostLocToXmm(UseImpl(arg.value, any_xmm));
}

OpArg RegAlloc::UseOpArg(Argument& arg) {
    return UseGpr(arg);
}

void RegAlloc::Use(Argument& arg, HostLoc host_loc) {
    ASSERT(!arg.allocated);
    arg.allocated = true;
    UseImpl(arg.value, {host_loc});
}

Xbyak::Reg64 RegAlloc::UseScratchGpr(Argument& arg) {
    ASSERT(!arg.allocated);
    arg.allocated = true;
    return HostLocToReg64(UseScratchImpl(arg.value, any_gpr));
}

Xbyak::Xmm RegAlloc::UseScratchXmm(Argument& arg) {
    ASSERT(!arg.allocated);
    arg.allocated = true;
    return HostLocToXmm(UseScratchImpl(arg.value, any_xmm));
}

void RegAlloc::UseScratch(Argument& arg, HostLoc host_loc) {
    ASSERT(!arg.allocated);
    arg.allocated = true;
    UseScratchImpl(arg.value, {host_loc});
}

void RegAlloc::DefineValue(IR::Inst* inst, const Xbyak::Reg& reg) {
    ASSERT(reg.getKind() == Xbyak::Operand::XMM || reg.getKind() == Xbyak::Operand::REG);
    HostLoc hostloc = static_cast<HostLoc>(reg.getIdx() + static_cast<size_t>(reg.getKind() == Xbyak::Operand::XMM ? HostLoc::XMM0 : HostLoc::RAX));
    DefineValueImpl(inst, hostloc);
}

void RegAlloc::DefineValue(IR::Inst* inst, Argument& arg) {
    ASSERT(!arg.allocated);
    arg.allocated = true;
    DefineValueImpl(inst, arg.value);
}

Xbyak::Reg64 RegAlloc::ScratchGpr(HostLocList desired_locations) {
    return HostLocToReg64(ScratchImpl(desired_locations));
}

Xbyak::Xmm RegAlloc::ScratchXmm(HostLocList desired_locations) {
    return HostLocToXmm(ScratchImpl(desired_locations));
}

HostLoc RegAlloc::UseImpl(IR::Value use_value, HostLocList desired_locations) {
    if (use_value.IsImmediate()) {
        return LoadImmediate(use_value, ScratchImpl(desired_locations));
    }

    IR::Inst* use_inst = use_value.GetInst();
    const HostLoc current_location = *ValueLocation(use_inst);

    const bool can_use_current_location = std::find(desired_locations.begin(), desired_locations.end(), current_location) != desired_locations.end();
    if (can_use_current_location) {
        LocInfo(current_location).ReadLock();
        return current_location;
    }

    if (LocInfo(current_location).IsLocked()) {
        return UseScratchImpl(use_value, desired_locations);
    }

    const HostLoc destination_location = SelectARegister(desired_locations);
    if (IsSameHostLocClass(destination_location, current_location)) {
        Exchange(destination_location, current_location);
    } else {
        MoveOutOfTheWay(destination_location);
        Move(destination_location, current_location);
    }
    LocInfo(destination_location).ReadLock();
    return destination_location;
}

HostLoc RegAlloc::UseScratchImpl(IR::Value use_value, HostLocList desired_locations) {
    if (use_value.IsImmediate()) {
        return LoadImmediate(use_value, ScratchImpl(desired_locations));
    }

    IR::Inst* use_inst = use_value.GetInst();
    const HostLoc current_location = *ValueLocation(use_inst);

    const bool can_use_current_location = std::find(desired_locations.begin(), desired_locations.end(), current_location) != desired_locations.end();
    if (can_use_current_location && !LocInfo(current_location).IsLocked()) {
        MoveOutOfTheWay(current_location);
        LocInfo(current_location).WriteLock();
        return current_location;
    }

    const HostLoc destination_location = SelectARegister(desired_locations);
    MoveOutOfTheWay(destination_location);
    CopyToScratch(destination_location, current_location);
    LocInfo(destination_location).WriteLock();
    return destination_location;
}

HostLoc RegAlloc::ScratchImpl(HostLocList desired_locations) {
    HostLoc location = SelectARegister(desired_locations);
    MoveOutOfTheWay(location);
    LocInfo(location).WriteLock();
    return location;
}

void RegAlloc::HostCall(IR::Inst* result_def, boost::optional<Argument&> arg0, boost::optional<Argument&> arg1, boost::optional<Argument&> arg2, boost::optional<Argument&> arg3) {
    constexpr size_t args_count = 4;
    constexpr std::array<HostLoc, args_count> args_hostloc = { ABI_PARAM1, ABI_PARAM2, ABI_PARAM3, ABI_PARAM4 };
    const std::array<boost::optional<Argument&>, args_count> args = { arg0, arg1, arg2, arg3 };

    static const std::vector<HostLoc> other_caller_save = [args_hostloc]() {
        std::vector<HostLoc> ret(ABI_ALL_CALLER_SAVE.begin(), ABI_ALL_CALLER_SAVE.end());

        for (auto hostloc : args_hostloc)
            ret.erase(std::find(ret.begin(), ret.end(), hostloc));

        return ret;
    }();

    ScratchGpr({ABI_RETURN});
    if (result_def) {
        DefineValueImpl(result_def, ABI_RETURN);
    }

    for (size_t i = 0; i < args_count; i++) {
        if (args[i]) {
            UseScratch(*args[i], args_hostloc[i]);
#if defined(__llvm__) && !defined(_WIN32)
            // LLVM puts the burden of zero-extension of 8 and 16 bit values on the caller instead of the callee
            Xbyak::Reg64 reg = HostLocToReg64(args_hostloc[i]);
            switch (args[i]->GetType()) {
            case IR::Type::U8:
                code->movzx(reg.cvt32(), reg.cvt8());
                break;
            case IR::Type::U16:
                code->movzx(reg.cvt32(), reg.cvt16());
                break;
            default:
                break; // Nothing needs to be done
            }
#endif
        }
    }

    for (size_t i = 0; i < args_count; i++) {
        if (!args[i]) {
            // TODO: Force spill
            ScratchGpr({args_hostloc[i]});
        }
    }

    for (HostLoc caller_saved : other_caller_save) {
        ScratchImpl({caller_saved});
    }
}

void RegAlloc::EndOfAllocScope() {
    for (auto& iter : hostloc_info) {
        iter.EndOfAllocScope();
    }
}

void RegAlloc::AssertNoMoreUses() {
    ASSERT(std::all_of(hostloc_info.begin(), hostloc_info.end(), [](const auto& i) { return i.IsEmpty(); }));
}

HostLoc RegAlloc::SelectARegister(HostLocList desired_locations) const {
    std::vector<HostLoc> candidates = desired_locations;

    // Find all locations that have not been allocated..
    auto allocated_locs = std::partition(candidates.begin(), candidates.end(), [this](auto loc){
        return !this->LocInfo(loc).IsLocked();
    });
    candidates.erase(allocated_locs, candidates.end());
    ASSERT_MSG(!candidates.empty(), "All candidate registers have already been allocated");

    // Selects the best location out of the available locations.
    // TODO: Actually do LRU or something. Currently we just try to pick something without a value if possible.

    std::partition(candidates.begin(), candidates.end(), [this](auto loc){
        return this->LocInfo(loc).IsEmpty();
    });

    return candidates.front();
}

boost::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const {
    for (size_t i = 0; i < HostLocCount; i++)
        if (hostloc_info[i].ContainsValue(value))
            return boost::make_optional<HostLoc>(static_cast<HostLoc>(i));

    return boost::none;
}

void RegAlloc::DefineValueImpl(IR::Inst* def_inst, HostLoc host_loc) {
    ASSERT_MSG(!ValueLocation(def_inst), "def_inst has already been defined");
    LocInfo(host_loc).AddValue(def_inst);
}

void RegAlloc::DefineValueImpl(IR::Inst* def_inst, const IR::Value& use_inst) {
    ASSERT_MSG(!ValueLocation(def_inst), "def_inst has already been defined");

    if (use_inst.IsImmediate()) {
        HostLoc location = ScratchImpl(any_gpr);
        DefineValueImpl(def_inst, location);
        LoadImmediate(use_inst, location);
        return;
    }

    ASSERT_MSG(ValueLocation(use_inst.GetInst()), "use_inst must already be defined");
    HostLoc location = *ValueLocation(use_inst.GetInst());
    DefineValueImpl(def_inst, location);
}

HostLoc RegAlloc::LoadImmediate(IR::Value imm, HostLoc host_loc) {
    ASSERT_MSG(imm.IsImmediate(), "imm is not an immediate");

    if (HostLocIsGPR(host_loc)) {
        Xbyak::Reg64 reg = HostLocToReg64(host_loc);
        u64 imm_value = ImmediateToU64(imm);
        if (imm_value == 0)
            code->xor_(reg.cvt32(), reg.cvt32());
        else
            code->mov(reg, imm_value);
        return host_loc;
    }

    if (HostLocIsXMM(host_loc)) {
        Xbyak::Xmm reg = HostLocToXmm(host_loc);
        u64 imm_value = ImmediateToU64(imm);
        if (imm_value == 0)
            code->pxor(reg, reg);
        else
            code->movdqa(reg, code->MConst(imm_value)); // TODO: movaps/movapd more appropriate sometimes
        return host_loc;
    }

    UNREACHABLE();
}

void RegAlloc::Move(HostLoc to, HostLoc from) {
    ASSERT(LocInfo(to).IsEmpty() && !LocInfo(from).IsLocked());

    if (LocInfo(from).IsEmpty()) {
        return;
    }

    LocInfo(to) = LocInfo(from);
    LocInfo(from) = {};

    EmitMove(code, to, from);
}

void RegAlloc::CopyToScratch(HostLoc to, HostLoc from) {
    ASSERT(LocInfo(to).IsEmpty() && !LocInfo(from).IsEmpty());

    EmitMove(code, to, from);
}

void RegAlloc::Exchange(HostLoc a, HostLoc b) {
    ASSERT(!LocInfo(a).IsLocked() && !LocInfo(b).IsLocked());

    if (LocInfo(a).IsEmpty()) {
        Move(a, b);
        return;
    }

    if (LocInfo(b).IsEmpty()) {
        Move(b, a);
        return;
    }

    std::swap(LocInfo(a), LocInfo(b));

    EmitExchange(code, a, b);
}

void RegAlloc::MoveOutOfTheWay(HostLoc reg) {
    ASSERT(!LocInfo(reg).IsLocked());
    if (!LocInfo(reg).IsEmpty()) {
        SpillRegister(reg);
    }
}

void RegAlloc::SpillRegister(HostLoc loc) {
    ASSERT_MSG(HostLocIsRegister(loc), "Only registers can be spilled");
    ASSERT_MSG(!LocInfo(loc).IsEmpty(), "There is no need to spill unoccupied registers");
    ASSERT_MSG(!LocInfo(loc).IsLocked(), "Registers that have been allocated must not be spilt");

    HostLoc new_loc = FindFreeSpill();
    Move(new_loc, loc);
}

HostLoc RegAlloc::FindFreeSpill() const {
    for (size_t i = 0; i < SpillCount; i++)
        if (LocInfo(HostLocSpill(i)).IsEmpty())
            return HostLocSpill(i);

    ASSERT_MSG(false, "All spill locations are full");
}

HostLocInfo& RegAlloc::LocInfo(HostLoc loc) {
    ASSERT(loc != HostLoc::RSP && loc != HostLoc::R15);
    return hostloc_info[static_cast<size_t>(loc)];
}

const HostLocInfo& RegAlloc::LocInfo(HostLoc loc) const {
    ASSERT(loc != HostLoc::RSP && loc != HostLoc::R15);
    return hostloc_info[static_cast<size_t>(loc)];
}

} // namespace BackendX64
} // namespace Dynarmic
