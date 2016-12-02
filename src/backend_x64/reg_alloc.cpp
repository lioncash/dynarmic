/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>

#include <xbyak.h>

#include "backend_x64/abi.h"
#include "backend_x64/jitstate.h"
#include "backend_x64/reg_alloc.h"
#include "common/assert.h"

namespace Dynarmic {
namespace BackendX64 {

static u32 ImmediateToU32(const IR::Value& imm) {
    switch (imm.GetType()) {
    case IR::Type::U1:
        return u32(imm.GetU1());
    case IR::Type::U8:
        return u32(imm.GetU8());
    case IR::Type::U32:
        return u32(imm.GetU32());
    default:
        ASSERT_MSG(false, "This should never happen.");
    }
}

static Xbyak::Reg HostLocToX64(HostLoc hostloc) {
    if (HostLocIsGPR(hostloc)) {
        DEBUG_ASSERT(hostloc != HostLoc::RSP && hostloc != HostLoc::R15);
        return HostLocToReg64(hostloc);
    }
    if (HostLocIsXMM(hostloc)) {
        return HostLocToXmm(hostloc);
    }
    ASSERT_MSG(false, "This should never happen.");
}

HostLoc RegAlloc::DefHostLocReg(IR::Inst* def_inst, HostLocList desired_locations) {
    DEBUG_ASSERT(std::all_of(desired_locations.begin(), desired_locations.end(), HostLocIsRegister));
    DEBUG_ASSERT_MSG(!ValueLocation(def_inst), "def_inst has already been defined");

    HostLoc location = SelectARegister(desired_locations);

    if (IsRegisterOccupied(location)) {
        SpillRegister(location);
    }

    LocInfo(location).is_being_used = true;
    LocInfo(location).def = def_inst;

    DEBUG_ASSERT(LocInfo(location).IsDef());
    return location;
}

void RegAlloc::RegisterAddDef(IR::Inst* def_inst, const IR::Value& use_inst) {
    DEBUG_ASSERT_MSG(!ValueLocation(def_inst), "def_inst has already been defined");

    if (use_inst.IsImmediate()) {
        LoadImmediateIntoHostLocReg(use_inst, DefHostLocReg(def_inst, any_gpr));
        return;
    }

    DEBUG_ASSERT_MSG(ValueLocation(use_inst.GetInst()), "use_inst must already be defined");
    HostLoc location = *ValueLocation(use_inst.GetInst());
    LocInfo(location).values.emplace_back(def_inst);
    use_inst.GetInst()->DecrementRemainingUses();
    DEBUG_ASSERT(LocInfo(location).IsIdle());
}

HostLoc RegAlloc::UseDefHostLocReg(IR::Value use_value, IR::Inst* def_inst, HostLocList desired_locations) {
    if (!use_value.IsImmediate()) {
        return UseDefHostLocReg(use_value.GetInst(), def_inst, desired_locations);
    }

    return LoadImmediateIntoHostLocReg(use_value, DefHostLocReg(def_inst, desired_locations));
}

HostLoc RegAlloc::UseDefHostLocReg(IR::Inst* use_inst, IR::Inst* def_inst, HostLocList desired_locations) {
    DEBUG_ASSERT(std::all_of(desired_locations.begin(), desired_locations.end(), HostLocIsRegister));
    DEBUG_ASSERT_MSG(!ValueLocation(def_inst), "def_inst has already been defined");
    DEBUG_ASSERT_MSG(ValueLocation(use_inst), "use_inst has not been defined");

    if (IsLastUse(use_inst)) {
        HostLoc current_location = *ValueLocation(use_inst);
        auto& loc_info = LocInfo(current_location);
        if (loc_info.IsIdle()) {
            loc_info.is_being_used = true;
            loc_info.def = def_inst;
            DEBUG_ASSERT(loc_info.IsUseDef());
            if (HostLocIsSpill(current_location)) {
                HostLoc new_location = SelectARegister(desired_locations);
                if (IsRegisterOccupied(new_location)) {
                    SpillRegister(new_location);
                }
                EmitMove(new_location, current_location);
                LocInfo(new_location) = LocInfo(current_location);
                LocInfo(current_location) = {};
                return new_location;
            } else {
                return current_location;
            }
        }
    }

    bool is_floating_point = HostLocIsXMM(*desired_locations.begin());
    if (is_floating_point) {
        DEBUG_ASSERT(use_inst->GetType() == IR::Type::F32 || use_inst->GetType() == IR::Type::F64);
    }
    HostLoc use_reg = UseHostLocReg(use_inst, is_floating_point ? any_xmm : any_gpr);
    HostLoc def_reg = DefHostLocReg(def_inst, desired_locations);
    if (is_floating_point) {
        code->movapd(HostLocToXmm(def_reg), HostLocToXmm(use_reg));
    } else {
        code->mov(HostLocToReg64(def_reg), HostLocToReg64(use_reg));
    }
    return def_reg;
}

std::tuple<OpArg, HostLoc> RegAlloc::UseDefOpArgHostLocReg(IR::Value use_value, IR::Inst* def_inst, HostLocList desired_locations) {
    DEBUG_ASSERT(std::all_of(desired_locations.begin(), desired_locations.end(), HostLocIsRegister));
    DEBUG_ASSERT_MSG(!ValueLocation(def_inst), "def_inst has already been defined");
    DEBUG_ASSERT_MSG(use_value.IsImmediate() || ValueLocation(use_value.GetInst()), "use_inst has not been defined");

    if (!use_value.IsImmediate()) {
        const IR::Inst* use_inst = use_value.GetInst();

        if (IsLastUse(use_inst)) {
            HostLoc current_location = *ValueLocation(use_inst);
            auto& loc_info = LocInfo(current_location);
            if (!loc_info.IsIdle()) {
                if (HostLocIsSpill(current_location)) {
                    loc_info.is_being_used = true;
                    DEBUG_ASSERT(loc_info.IsUse());
                    return std::make_tuple(SpillToOpArg(current_location), DefHostLocReg(def_inst, desired_locations));
                } else {
                    loc_info.is_being_used = true;
                    loc_info.def = def_inst;
                    DEBUG_ASSERT(loc_info.IsUseDef());
                    return std::make_tuple(HostLocToX64(current_location), current_location);
                }
            }
        }
    }

    OpArg use_oparg = UseOpArg(use_value, any_gpr);
    HostLoc def_reg = DefHostLocReg(def_inst, desired_locations);
    return std::make_tuple(use_oparg, def_reg);
}

HostLoc RegAlloc::UseHostLocReg(IR::Value use_value, HostLocList desired_locations) {
    if (!use_value.IsImmediate()) {
        return UseHostLocReg(use_value.GetInst(), desired_locations);
    }

    return LoadImmediateIntoHostLocReg(use_value, ScratchHostLocReg(desired_locations));
}

HostLoc RegAlloc::UseHostLocReg(IR::Inst* use_inst, HostLocList desired_locations) {
    HostLoc current_location;
    bool was_being_used;
    std::tie(current_location, was_being_used) = UseHostLoc(use_inst, desired_locations);

    if (HostLocIsRegister(current_location)) {
        return current_location;
    } else if (HostLocIsSpill(current_location)) {
        HostLoc new_location = SelectARegister(desired_locations);
        if (IsRegisterOccupied(new_location)) {
            SpillRegister(new_location);
        }
        EmitMove(new_location, current_location);
        if (!was_being_used) {
            LocInfo(new_location) = LocInfo(current_location);
            LocInfo(current_location) = {};
            DEBUG_ASSERT(LocInfo(new_location).IsUse());
        } else {
            LocInfo(new_location).is_being_used = true;
            DEBUG_ASSERT(LocInfo(new_location).IsScratch());
        }
        return new_location;
    }

    ASSERT_MSG(false, "Unknown current_location type");
}

OpArg RegAlloc::UseOpArg(IR::Value use_value, HostLocList desired_locations) {
    if (use_value.IsImmediate()) {
        ASSERT_MSG(false, "UseOpArg does not support immediates");
        return {}; // return a None
    }

    IR::Inst* use_inst = use_value.GetInst();

    HostLoc current_location;
    bool was_being_used;
    std::tie(current_location, was_being_used) = UseHostLoc(use_inst, desired_locations);

    if (HostLocIsRegister(current_location)) {
        return HostLocToX64(current_location);
    } else if (HostLocIsSpill(current_location)) {
        return SpillToOpArg(current_location);
    }

    ASSERT_MSG(false, "Unknown current_location type");
}

HostLoc RegAlloc::UseScratchHostLocReg(IR::Value use_value, HostLocList desired_locations) {
    if (!use_value.IsImmediate()) {
        return UseScratchHostLocReg(use_value.GetInst(), desired_locations);
    }

    return LoadImmediateIntoHostLocReg(use_value, ScratchHostLocReg(desired_locations));
}

HostLoc RegAlloc::UseScratchHostLocReg(IR::Inst* use_inst, HostLocList desired_locations) {
    DEBUG_ASSERT(std::all_of(desired_locations.begin(), desired_locations.end(), HostLocIsRegister));
    DEBUG_ASSERT_MSG(ValueLocation(use_inst), "use_inst has not been defined");
    ASSERT_MSG(use_inst->HasUses(), "use_inst ran out of uses. (Use-d an IR::Inst* too many times)");

    HostLoc current_location = *ValueLocation(use_inst);
    HostLoc new_location = SelectARegister(desired_locations);
    if (IsRegisterOccupied(new_location)) {
        SpillRegister(new_location);
    }

    if (HostLocIsSpill(current_location)) {
        EmitMove(new_location, current_location);
        LocInfo(new_location).is_being_used = true;
        use_inst->DecrementRemainingUses();
        DEBUG_ASSERT(LocInfo(new_location).IsScratch());
        return new_location;
    } else if (HostLocIsRegister(current_location)) {
        ASSERT(LocInfo(current_location).IsIdle()
                || LocInfo(current_location).IsUse()
                || LocInfo(current_location).IsUseDef());

        if (current_location != new_location) {
            EmitMove(new_location, current_location);
        } else {
            ASSERT(LocInfo(current_location).IsIdle());
        }

        LocInfo(new_location).is_being_used = true;
        LocInfo(new_location).values.clear();
        use_inst->DecrementRemainingUses();
        DEBUG_ASSERT(LocInfo(new_location).IsScratch());
        return new_location;
    }

    ASSERT_MSG(false, "Invalid current_location");
}

HostLoc RegAlloc::ScratchHostLocReg(HostLocList desired_locations) {
    DEBUG_ASSERT(std::all_of(desired_locations.begin(), desired_locations.end(), HostLocIsRegister));

    HostLoc location = SelectARegister(desired_locations);

    if (IsRegisterOccupied(location)) {
        SpillRegister(location);
    }

    // Update state
    LocInfo(location).is_being_used = true;

    DEBUG_ASSERT(LocInfo(location).IsScratch());
    return location;
}

void RegAlloc::HostCall(IR::Inst* result_def, IR::Value arg0_use, IR::Value arg1_use, IR::Value arg2_use, IR::Value arg3_use) {
    constexpr size_t args_count = 4;
    constexpr std::array<HostLoc, args_count> args_hostloc = { ABI_PARAM1, ABI_PARAM2, ABI_PARAM3, ABI_PARAM4 };
    const std::array<IR::Value*, args_count> args = {&arg0_use, &arg1_use, &arg2_use, &arg3_use};

    const static std::vector<HostLoc> other_caller_save = [args_hostloc](){
        std::vector<HostLoc> ret(ABI_ALL_CALLER_SAVE.begin(), ABI_ALL_CALLER_SAVE.end());

        for (auto hostloc : args_hostloc)
            ret.erase(std::find(ret.begin(), ret.end(), hostloc));

        return ret;
    }();

    // TODO: This works but almost certainly leads to suboptimal generated code.

    if (result_def) {
        DefHostLocReg(result_def, {ABI_RETURN});
    } else {
        ScratchHostLocReg({ABI_RETURN});
    }

    for (size_t i = 0; i < args_count; i++) {
        if (!args[i]->IsEmpty()) {
            UseScratchHostLocReg(*args[i], {args_hostloc[i]});
        } else {
            ScratchHostLocReg({args_hostloc[i]});
        }
    }

    for (HostLoc caller_saved : other_caller_save) {
        ScratchHostLocReg({caller_saved});
    }
}

HostLoc RegAlloc::SelectARegister(HostLocList desired_locations) const {
    std::vector<HostLoc> candidates = desired_locations;

    // Find all locations that have not been allocated..
    auto allocated_locs = std::partition(candidates.begin(), candidates.end(), [this](auto loc){
        return !this->IsRegisterAllocated(loc);
    });
    candidates.erase(allocated_locs, candidates.end());
    ASSERT_MSG(!candidates.empty(), "All candidate registers have already been allocated");

    // Selects the best location out of the available locations.
    // TODO: Actually do LRU or something. Currently we just try to pick something without a value if possible.

    std::partition(candidates.begin(), candidates.end(), [this](auto loc){
        return !this->IsRegisterOccupied(loc);
    });

    return candidates.front();
}

boost::optional<HostLoc> RegAlloc::ValueLocation(const IR::Inst* value) const {
    for (size_t i = 0; i < HostLocCount; i++)
        for (const IR::Inst* v : hostloc_info[i].values)
            if (v == value)
                return boost::make_optional<HostLoc>(static_cast<HostLoc>(i));

    return boost::none;
}

bool RegAlloc::IsRegisterOccupied(HostLoc loc) const {
    const auto& info = LocInfo(loc);

    return !info.values.empty() || info.def;
}

bool RegAlloc::IsRegisterAllocated(HostLoc loc) const {
    return LocInfo(loc).is_being_used;
}

bool RegAlloc::IsLastUse(const IR::Inst* inst) const {
    if (inst->UseCount() > 1)
        return false;
    return LocInfo(*ValueLocation(inst)).values.size() == 1;
}

void RegAlloc::SpillRegister(HostLoc loc) {
    ASSERT_MSG(HostLocIsRegister(loc), "Only registers can be spilled");
    ASSERT_MSG(IsRegisterOccupied(loc), "There is no need to spill unoccupied registers");
    ASSERT_MSG(!IsRegisterAllocated(loc), "Registers that have been allocated must not be spilt");

    HostLoc new_loc = FindFreeSpill();

    EmitMove(new_loc, loc);

    LocInfo(new_loc) = LocInfo(loc);
    LocInfo(loc) = {};
}

HostLoc RegAlloc::FindFreeSpill() const {
    for (size_t i = 0; i < SpillCount; i++)
        if (!IsRegisterOccupied(HostLocSpill(i)))
            return HostLocSpill(i);

    ASSERT_MSG(false, "All spill locations are full");
}

void RegAlloc::EndOfAllocScope() {
    for (auto& iter : hostloc_info) {
        iter.is_being_used = false;
        if (iter.def) {
            iter.values.clear();
            iter.values.emplace_back(iter.def);
            iter.def = nullptr;
        }
        if (!iter.values.empty()) {
            auto to_erase = std::remove_if(iter.values.begin(), iter.values.end(),
                                           [](const auto& inst){ return !inst->HasUses(); });
            iter.values.erase(to_erase, iter.values.end());
        }
    }
}

void RegAlloc::AssertNoMoreUses() {
    ASSERT(std::all_of(hostloc_info.begin(), hostloc_info.end(), [](const auto& i){ return i.values.empty(); }));
}

void RegAlloc::Reset() {
    hostloc_info.fill({});
}

void RegAlloc::EmitMove(HostLoc to, HostLoc from) {
    if (HostLocIsXMM(to) && HostLocIsSpill(from)) {
        code->movsd(HostLocToXmm(to), SpillToOpArg(from));
    } else if (HostLocIsSpill(to) && HostLocIsXMM(from)) {
        code->movsd(SpillToOpArg(to), HostLocToXmm(from));
    } else if (HostLocIsXMM(to) && HostLocIsXMM(from)) {
        code->movaps(HostLocToXmm(to), HostLocToXmm(from));
    } else if (HostLocIsGPR(to) && HostLocIsSpill(from)) {
        code->mov(HostLocToReg64(to), SpillToOpArg(from));
    } else if (HostLocIsSpill(to) && HostLocIsGPR(from)) {
        code->mov(SpillToOpArg(to), HostLocToReg64(from));
    } else if (HostLocIsGPR(to) && HostLocIsGPR(from)){
        code->mov(HostLocToReg64(to), HostLocToReg64(from));
    } else {
        ASSERT_MSG(false, "Invalid RegAlloc::EmitMove");
    }
}

void RegAlloc::EmitExchange(HostLoc a, HostLoc b) {
    if (HostLocIsGPR(a) && HostLocIsGPR(b)) {
        code->xchg(HostLocToReg64(a), HostLocToReg64(b));
    } else if (HostLocIsXMM(a) && HostLocIsXMM(b)) {
        ASSERT_MSG(false, "Exchange is unnecessary for XMM registers");
    } else {
        ASSERT_MSG(false, "Invalid RegAlloc::EmitExchange");
    }
}

std::tuple<HostLoc, bool> RegAlloc::UseHostLoc(IR::Inst* use_inst, HostLocList desired_locations) {
    DEBUG_ASSERT(std::all_of(desired_locations.begin(), desired_locations.end(), HostLocIsRegister));
    DEBUG_ASSERT_MSG(ValueLocation(use_inst), "use_inst has not been defined");

    HostLoc current_location = *ValueLocation(use_inst);
    auto iter = std::find(desired_locations.begin(), desired_locations.end(), current_location);
    if (iter != desired_locations.end()) {
        if (LocInfo(current_location).IsDef()) {
            HostLoc new_location = SelectARegister(desired_locations);
            if (IsRegisterOccupied(new_location)) {
                SpillRegister(new_location);
            }
            EmitMove(new_location, current_location);
            LocInfo(new_location).is_being_used = true;
            LocInfo(new_location).values.emplace_back(use_inst);
            use_inst->DecrementRemainingUses();
            DEBUG_ASSERT(LocInfo(new_location).IsUse());
            return std::make_tuple(new_location, false);
        } else {
            bool was_being_used = LocInfo(current_location).is_being_used;
            ASSERT(LocInfo(current_location).IsUse() || LocInfo(current_location).IsIdle());
            LocInfo(current_location).is_being_used = true;
            use_inst->DecrementRemainingUses();
            DEBUG_ASSERT(LocInfo(current_location).IsUse());
            return std::make_tuple(current_location, was_being_used);
        }
    }

    if (HostLocIsSpill(current_location)) {
        bool was_being_used = LocInfo(current_location).is_being_used;
        LocInfo(current_location).is_being_used = true;
        use_inst->DecrementRemainingUses();
        DEBUG_ASSERT(LocInfo(current_location).IsUse());
        return std::make_tuple(current_location, was_being_used);
    } else if (HostLocIsRegister(current_location)) {
        HostLoc new_location = SelectARegister(desired_locations);
        ASSERT(LocInfo(current_location).IsIdle());
        EmitExchange(new_location, current_location);
        std::swap(LocInfo(new_location), LocInfo(current_location));
        LocInfo(new_location).is_being_used = true;
        use_inst->DecrementRemainingUses();
        DEBUG_ASSERT(LocInfo(new_location).IsUse());
        return std::make_tuple(new_location, false);
    }

    ASSERT_MSG(false, "Invalid current_location");
    return std::make_tuple(static_cast<HostLoc>(-1), false);
}

HostLoc RegAlloc::LoadImmediateIntoHostLocReg(IR::Value imm, HostLoc host_loc) {
    ASSERT_MSG(imm.IsImmediate(), "imm is not an immediate");

    Xbyak::Reg64 reg = HostLocToReg64(host_loc);

    u32 imm_value = ImmediateToU32(imm);
    if (imm_value == 0)
        code->xor_(reg, reg);
    else
        code->mov(reg.cvt32(), imm_value);
    return host_loc;
}

} // namespace BackendX64
} // namespace Dynarmic
