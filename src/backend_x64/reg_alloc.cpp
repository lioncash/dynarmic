/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>
#include <numeric>

#include <xbyak.h>

#include "backend_x64/abi.h"
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

// Minimum number of bits required to represent a type
static size_t GetBitWidth(IR::Type type) {
    switch (type) {
        case IR::Type::A32Reg:
        case IR::Type::A32ExtReg:
        case IR::Type::A64Reg:
        case IR::Type::A64Vec:
        case IR::Type::CoprocInfo:
        case IR::Type::Cond:
        case IR::Type::Void:
            ASSERT_MSG(false, "Type %zu cannot be represented at runtime", static_cast<size_t>(type));
            return 0;
        case IR::Type::Opaque:
            ASSERT_MSG(false, "Not a concrete type");
            return 0;
        case IR::Type::U1:
            return 8;
        case IR::Type::U8:
            return 8;
        case IR::Type::U16:
            return 16;
        case IR::Type::U32:
            return 32;
        case IR::Type::U64:
            return 64;
        case IR::Type::F32:
            return 32;
        case IR::Type::F64:
            return 64;
        case IR::Type::F128:
            return 128;
        case IR::Type::NZCVFlags:
            return 32; // TODO: Update to 16 when flags optimization is done
    }
}

bool HostLocInfo::IsLocked() const {
    return is_being_used;
}

bool HostLocInfo::IsEmpty() const {
    return !is_being_used && values.empty();
}

bool HostLocInfo::IsLastUse() const {
    return !is_being_used && current_references == 1 && accumulated_uses + 1 == total_uses;
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

void HostLocInfo::AddArgReference() {
    current_references++;
    ASSERT(accumulated_uses + current_references <= total_uses);
}

void HostLocInfo::EndOfAllocScope() {
    accumulated_uses += current_references;
    current_references = 0;

    if (total_uses == accumulated_uses) {
        values.clear();
        accumulated_uses = 0;
        total_uses = 0;
        max_bit_width = 0;
    }

    ASSERT(total_uses == std::accumulate(values.begin(), values.end(), size_t(0), [](size_t sum, IR::Inst* inst) { return sum + inst->UseCount(); }));

    is_being_used = false;
    is_scratch = false;
}

bool HostLocInfo::ContainsValue(const IR::Inst* inst) const {
    return std::find(values.begin(), values.end(), inst) != values.end();
}

size_t HostLocInfo::GetMaxBitWidth() const {
    return max_bit_width;
}

void HostLocInfo::AddValue(IR::Inst* inst) {
    values.push_back(inst);
    total_uses += inst->UseCount();
    max_bit_width = std::max(max_bit_width, GetBitWidth(inst->GetType()));
}

IR::Type Argument::GetType() const {
    return value.GetType();
}

bool Argument::IsImmediate() const {
    return value.IsImmediate();
}

bool Argument::FitsInImmediateU32() const {
    if (!IsImmediate())
        return false;
    u64 imm = ImmediateToU64(value);
    return imm < 0x100000000;
}

bool Argument::FitsInImmediateS32() const {
    if (!IsImmediate())
        return false;
    s64 imm = static_cast<s64>(ImmediateToU64(value));
    return -s64(0x80000000) <= imm && imm <= s64(0x7FFFFFFF);
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

u64 Argument::GetImmediateS32() const {
    ASSERT(FitsInImmediateS32());
    u64 imm = ImmediateToU64(value);
    return imm;
}

u64 Argument::GetImmediateU64() const {
    return ImmediateToU64(value);
}

IR::Cond Argument::GetImmediateCond() const {
    ASSERT(IsImmediate() && GetType() == IR::Type::Cond);
    return value.GetCond();
}

bool Argument::IsInGpr() const {
    if (IsImmediate())
        return false;
    return HostLocIsGPR(*reg_alloc.ValueLocation(value.GetInst()));
}

bool Argument::IsInXmm() const {
    if (IsImmediate())
        return false;
    return HostLocIsXMM(*reg_alloc.ValueLocation(value.GetInst()));
}

bool Argument::IsInMemory() const {
    if (IsImmediate())
        return false;
    return HostLocIsSpill(*reg_alloc.ValueLocation(value.GetInst()));
}

std::array<Argument, 3> RegAlloc::GetArgumentInfo(IR::Inst* inst) {
    std::array<Argument, 3> ret = { Argument{*this}, Argument{*this}, Argument{*this} };
    for (size_t i = 0; i < inst->NumArgs(); i++) {
        const IR::Value& arg = inst->GetArg(i);
        ret[i].value = arg;
        if (!arg.IsImmediate()) {
            ASSERT_MSG(ValueLocation(arg.GetInst()), "argument must already been defined");
            LocInfo(*ValueLocation(arg.GetInst())).AddArgReference();
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
        if (!LocInfo(current_location).IsLastUse()) {
            MoveOutOfTheWay(current_location);
        }
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
    for (size_t i = 0; i < hostloc_info.size(); i++)
        if (hostloc_info[i].ContainsValue(value))
            return static_cast<HostLoc>(i);

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
    ASSERT(LocInfo(from).GetMaxBitWidth() <= HostLocBitWidth(to));

    if (LocInfo(from).IsEmpty()) {
        return;
    }

    EmitMove(to, from);

    LocInfo(to) = LocInfo(from);
    LocInfo(from) = {};
}

void RegAlloc::CopyToScratch(HostLoc to, HostLoc from) {
    ASSERT(LocInfo(to).IsEmpty() && !LocInfo(from).IsEmpty());

    EmitMove(to, from);
}

void RegAlloc::Exchange(HostLoc a, HostLoc b) {
    ASSERT(!LocInfo(a).IsLocked() && !LocInfo(b).IsLocked());
    ASSERT(LocInfo(a).GetMaxBitWidth() <= HostLocBitWidth(b));
    ASSERT(LocInfo(b).GetMaxBitWidth() <= HostLocBitWidth(a));

    if (LocInfo(a).IsEmpty()) {
        Move(a, b);
        return;
    }

    if (LocInfo(b).IsEmpty()) {
        Move(b, a);
        return;
    }

    EmitExchange(a, b);

    std::swap(LocInfo(a), LocInfo(b));
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
    for (size_t i = static_cast<size_t>(HostLoc::FirstSpill); i < hostloc_info.size(); i++) {
        HostLoc loc = static_cast<HostLoc>(i);
        if (LocInfo(loc).IsEmpty())
            return loc;
    }

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

void RegAlloc::EmitMove(HostLoc to, HostLoc from) {
    const size_t bit_width = LocInfo(from).GetMaxBitWidth();

    if (HostLocIsXMM(to) && HostLocIsXMM(from)) {
        code->movaps(HostLocToXmm(to), HostLocToXmm(from));
    } else if (HostLocIsGPR(to) && HostLocIsGPR(from)) {
        ASSERT(bit_width != 128);
        if (bit_width == 64) {
            code->mov(HostLocToReg64(to), HostLocToReg64(from));
        } else {
            code->mov(HostLocToReg64(to).cvt32(), HostLocToReg64(from).cvt32());
        }
    } else if (HostLocIsXMM(to) && HostLocIsGPR(from)) {
        ASSERT(bit_width != 128);
        if (bit_width == 64) {
            code->movq(HostLocToXmm(to), HostLocToReg64(from));
        } else {
            code->movd(HostLocToXmm(to), HostLocToReg64(from).cvt32());
        }
    } else if (HostLocIsGPR(to) && HostLocIsXMM(from)) {
        ASSERT(bit_width != 128);
        if (bit_width == 64) {
            code->movq(HostLocToReg64(to), HostLocToXmm(from));
        } else {
            code->movd(HostLocToReg64(to).cvt32(), HostLocToXmm(from));
        }
    } else if (HostLocIsXMM(to) && HostLocIsSpill(from)) {
        Xbyak::Address spill_addr = spill_to_addr(from);
        ASSERT(spill_addr.getBit() >= bit_width);
        switch (bit_width) {
        case 128:
            code->movaps(HostLocToXmm(to), spill_addr);
            break;
        case 64:
            code->movsd(HostLocToXmm(to), spill_addr);
            break;
        case 32:
        case 16:
        case 8:
            code->movss(HostLocToXmm(to), spill_addr);
            break;
        default:
            UNREACHABLE();
        }
    } else if (HostLocIsSpill(to) && HostLocIsXMM(from)) {
        Xbyak::Address spill_addr = spill_to_addr(to);
        ASSERT(spill_addr.getBit() >= bit_width);
        switch (bit_width) {
        case 128:
            code->movaps(spill_addr, HostLocToXmm(from));
            break;
        case 64:
            code->movsd(spill_addr, HostLocToXmm(from));
            break;
        case 32:
        case 16:
        case 8:
            code->movss(spill_addr, HostLocToXmm(from));
            break;
        default:
            UNREACHABLE();
        }
    } else if (HostLocIsGPR(to) && HostLocIsSpill(from)) {
        ASSERT(bit_width != 128);
        if (bit_width == 64) {
            code->mov(HostLocToReg64(to), spill_to_addr(from));
        } else {
            code->mov(HostLocToReg64(to).cvt32(), spill_to_addr(from));
        }
    } else if (HostLocIsSpill(to) && HostLocIsGPR(from)) {
        ASSERT(bit_width != 128);
        if (bit_width == 64) {
            code->mov(spill_to_addr(to), HostLocToReg64(from));
        } else {
            code->mov(spill_to_addr(to), HostLocToReg64(from).cvt32());
        }
    } else {
        ASSERT_MSG(false, "Invalid RegAlloc::EmitMove");
    }
}

void RegAlloc::EmitExchange(HostLoc a, HostLoc b) {
    if (HostLocIsGPR(a) && HostLocIsGPR(b)) {
        code->xchg(HostLocToReg64(a), HostLocToReg64(b));
    } else if (HostLocIsXMM(a) && HostLocIsXMM(b)) {
        ASSERT_MSG(false, "Check your code: Exchanging XMM registers is unnecessary");
    } else {
        ASSERT_MSG(false, "Invalid RegAlloc::EmitExchange");
    }
}

} // namespace BackendX64
} // namespace Dynarmic
