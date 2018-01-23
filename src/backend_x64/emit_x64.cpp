/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <unordered_map>
#include <unordered_set>

#include "backend_x64/block_of_code.h"
#include "backend_x64/emit_x64.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "common/variant_util.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

// TODO: Have ARM flags in host flags and not have them use up GPR registers unless necessary.
// TODO: Actually implement that proper instruction selector you've always wanted to sweetheart.

namespace Dynarmic {
namespace BackendX64 {

using namespace Xbyak::util;

EmitContext::EmitContext(RegAlloc& reg_alloc, IR::Block& block)
    : reg_alloc(reg_alloc), block(block) {}

void EmitContext::EraseInstruction(IR::Inst* inst) {
    block.Instructions().erase(inst);
    inst->ClearArgs();
}

template <typename JST>
EmitX64<JST>::EmitX64(BlockOfCode* code)
    : code(code) {}

template <typename JST>
EmitX64<JST>::~EmitX64() {}

template <typename JST>
boost::optional<typename EmitX64<JST>::BlockDescriptor> EmitX64<JST>::GetBasicBlock(IR::LocationDescriptor descriptor) const {
    auto iter = block_descriptors.find(descriptor);
    if (iter == block_descriptors.end())
        return boost::none;
    return iter->second;
}

template <typename JST>
void EmitX64<JST>::EmitVoid(EmitContext&, IR::Inst*) {
}

template <typename JST>
void EmitX64<JST>::EmitBreakpoint(EmitContext&, IR::Inst*) {
    code->int3();
}

template <typename JST>
void EmitX64<JST>::EmitIdentity(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (!args[0].IsImmediate()) {
        ctx.reg_alloc.DefineValue(inst, args[0]);
    }
}

template <typename JST>
void EmitX64<JST>::PushRSBHelper(Xbyak::Reg64 loc_desc_reg, Xbyak::Reg64 index_reg, IR::LocationDescriptor target) {
    using namespace Xbyak::util;

    auto iter = block_descriptors.find(target);
    CodePtr target_code_ptr = iter != block_descriptors.end()
                            ? iter->second.entrypoint
                            : code->GetReturnFromRunCodeAddress();

    code->mov(index_reg.cvt32(), dword[r15 + code->GetJitStateInfo().offsetof_rsb_ptr]);

    code->mov(loc_desc_reg, target.Value());

    patch_information[target].mov_rcx.emplace_back(code->getCurr());
    EmitPatchMovRcx(target_code_ptr);

    code->mov(qword[r15 + index_reg * 8 + code->GetJitStateInfo().offsetof_rsb_location_descriptors], loc_desc_reg);
    code->mov(qword[r15 + index_reg * 8 + code->GetJitStateInfo().offsetof_rsb_codeptrs], rcx);

    code->add(index_reg.cvt32(), 1);
    code->and_(index_reg.cvt32(), u32(code->GetJitStateInfo().rsb_ptr_mask));
    code->mov(dword[r15 + code->GetJitStateInfo().offsetof_rsb_ptr], index_reg.cvt32());
}

template <typename JST>
void EmitX64<JST>::EmitPushRSB(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    u64 unique_hash_of_target = args[0].GetImmediateU64();

    ctx.reg_alloc.ScratchGpr({HostLoc::RCX});
    Xbyak::Reg64 loc_desc_reg = ctx.reg_alloc.ScratchGpr();
    Xbyak::Reg64 index_reg = ctx.reg_alloc.ScratchGpr();

    PushRSBHelper(loc_desc_reg, index_reg, IR::LocationDescriptor{unique_hash_of_target});
}

template <typename JST>
void EmitX64<JST>::EmitGetCarryFromOp(EmitContext&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

template <typename JST>
void EmitX64<JST>::EmitGetOverflowFromOp(EmitContext&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

template <typename JST>
void EmitX64<JST>::EmitGetGEFromOp(EmitContext&, IR::Inst*) {
    ASSERT_MSG(false, "should never happen");
}

template <typename JST>
void EmitX64<JST>::EmitGetNZCVFromOp(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const int bitsize = [&]{
        switch (args[0].GetType()) {
        case IR::Type::U8:
            return 8;
        case IR::Type::U16:
            return 16;
        case IR::Type::U32:
            return 32;
        case IR::Type::U64:
            return 64;
        default:
            ASSERT_MSG(false, "Unreachable");
            return 0;
        }
    }();

    Xbyak::Reg64 nzcv = ctx.reg_alloc.ScratchGpr({HostLoc::RAX});
    Xbyak::Reg value = ctx.reg_alloc.UseGpr(args[0]).changeBit(bitsize);
    code->cmp(value, 0);
    code->lahf();
    code->seto(code->al);
    ctx.reg_alloc.DefineValue(inst, nzcv);
}

template <typename JST>
void EmitX64<JST>::EmitAddCycles(size_t cycles) {
    ASSERT(cycles < std::numeric_limits<u32>::max());
    code->sub(qword[r15 + code->GetJitStateInfo().offsetof_cycles_remaining], static_cast<u32>(cycles));
}

template <typename JST>
Xbyak::Label EmitX64<JST>::EmitCond(IR::Cond cond) {
    Xbyak::Label label;

    const Xbyak::Reg32 cpsr = eax;
    code->mov(cpsr, dword[r15 + code->GetJitStateInfo().offsetof_CPSR_nzcv]);

    constexpr size_t n_shift = 31;
    constexpr size_t z_shift = 30;
    constexpr size_t c_shift = 29;
    constexpr size_t v_shift = 28;
    constexpr u32 n_mask = 1u << n_shift;
    constexpr u32 z_mask = 1u << z_shift;
    constexpr u32 c_mask = 1u << c_shift;
    constexpr u32 v_mask = 1u << v_shift;

    switch (cond) {
    case IR::Cond::EQ: //z
        code->test(cpsr, z_mask);
        code->jnz(label);
        break;
    case IR::Cond::NE: //!z
        code->test(cpsr, z_mask);
        code->jz(label);
        break;
    case IR::Cond::CS: //c
        code->test(cpsr, c_mask);
        code->jnz(label);
        break;
    case IR::Cond::CC: //!c
        code->test(cpsr, c_mask);
        code->jz(label);
        break;
    case IR::Cond::MI: //n
        code->test(cpsr, n_mask);
        code->jnz(label);
        break;
    case IR::Cond::PL: //!n
        code->test(cpsr, n_mask);
        code->jz(label);
        break;
    case IR::Cond::VS: //v
        code->test(cpsr, v_mask);
        code->jnz(label);
        break;
    case IR::Cond::VC: //!v
        code->test(cpsr, v_mask);
        code->jz(label);
        break;
    case IR::Cond::HI: { //c & !z
        code->and_(cpsr, z_mask | c_mask);
        code->cmp(cpsr, c_mask);
        code->je(label);
        break;
    }
    case IR::Cond::LS: { //!c | z
        code->and_(cpsr, z_mask | c_mask);
        code->cmp(cpsr, c_mask);
        code->jne(label);
        break;
    }
    case IR::Cond::GE: { // n == v
        code->and_(cpsr, n_mask | v_mask);
        code->jz(label);
        code->cmp(cpsr, n_mask | v_mask);
        code->je(label);
        break;
    }
    case IR::Cond::LT: { // n != v
        Xbyak::Label fail;
        code->and_(cpsr, n_mask | v_mask);
        code->jz(fail);
        code->cmp(cpsr, n_mask | v_mask);
        code->jne(label);
        code->L(fail);
        break;
    }
    case IR::Cond::GT: { // !z & (n == v)
        const Xbyak::Reg32 tmp1 = ebx;
        const Xbyak::Reg32 tmp2 = esi;
        code->mov(tmp1, cpsr);
        code->mov(tmp2, cpsr);
        code->shr(tmp1, n_shift);
        code->shr(tmp2, v_shift);
        code->shr(cpsr, z_shift);
        code->xor_(tmp1, tmp2);
        code->or_(tmp1, cpsr);
        code->test(tmp1, 1);
        code->jz(label);
        break;
    }
    case IR::Cond::LE: { // z | (n != v)
        const Xbyak::Reg32 tmp1 = ebx;
        const Xbyak::Reg32 tmp2 = esi;
        code->mov(tmp1, cpsr);
        code->mov(tmp2, cpsr);
        code->shr(tmp1, n_shift);
        code->shr(tmp2, v_shift);
        code->shr(cpsr, z_shift);
        code->xor_(tmp1, tmp2);
        code->or_(tmp1, cpsr);
        code->test(tmp1, 1);
        code->jnz(label);
        break;
    }
    default:
        ASSERT_MSG(false, "Unknown cond %zu", static_cast<size_t>(cond));
        break;
    }

    return label;
}

template <typename JST>
void EmitX64<JST>::EmitCondPrelude(const IR::Block& block) {
    if (block.GetCondition() == IR::Cond::AL) {
        ASSERT(!block.HasConditionFailedLocation());
        return;
    }

    ASSERT(block.HasConditionFailedLocation());

    Xbyak::Label pass = EmitCond(block.GetCondition());
    EmitAddCycles(block.ConditionFailedCycleCount());
    EmitTerminal(IR::Term::LinkBlock{block.ConditionFailedLocation()}, block.Location());
    code->L(pass);
}

template <typename JST>
void EmitX64<JST>::EmitTerminal(IR::Terminal terminal, IR::LocationDescriptor initial_location) {
    Common::VisitVariant<void>(terminal, [this, &initial_location](auto x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (!std::is_same_v<T, IR::Term::Invalid>) {
            this->EmitTerminalImpl(x, initial_location);
        } else {
            ASSERT_MSG(false, "Invalid terminal");
        }
    });
}

template <typename JST>
void EmitX64<JST>::Patch(const IR::LocationDescriptor& desc, CodePtr bb) {
    const CodePtr save_code_ptr = code->getCurr();
    const PatchInformation& patch_info = patch_information[desc];

    for (CodePtr location : patch_info.jg) {
        code->SetCodePtr(location);
        EmitPatchJg(desc, bb);
    }

    for (CodePtr location : patch_info.jmp) {
        code->SetCodePtr(location);
        EmitPatchJmp(desc, bb);
    }

    for (CodePtr location : patch_info.mov_rcx) {
        code->SetCodePtr(location);
        EmitPatchMovRcx(bb);
    }

    code->SetCodePtr(save_code_ptr);
}

template <typename JST>
void EmitX64<JST>::Unpatch(const IR::LocationDescriptor& desc) {
    Patch(desc, nullptr);
}

template <typename JST>
void EmitX64<JST>::ClearCache() {
    block_ranges.clear();
    block_descriptors.clear();
    patch_information.clear();
}

template <typename JST>
void EmitX64<JST>::InvalidateCacheRanges(const boost::icl::interval_set<ProgramCounterType>& ranges) {
    // Remove cached block descriptors and patch information overlapping with the given range.
    std::unordered_set<IR::LocationDescriptor> erase_locations;
    for (auto invalidate_interval : ranges) {
        auto pair = block_ranges.equal_range(invalidate_interval);
        for (auto it = pair.first; it != pair.second; ++it) {
            for (const auto &descriptor : it->second) {
                erase_locations.insert(descriptor);
            }
        }
    }
    for (const auto &descriptor : erase_locations) {
        auto it = block_descriptors.find(descriptor);
        if (it == block_descriptors.end()) {
            continue;
        }

        if (patch_information.count(descriptor)) {
            Unpatch(descriptor);
        }
        block_ranges.subtract(std::make_pair(it->second.range, std::set<IR::LocationDescriptor>{descriptor}));
        block_descriptors.erase(it);
    }
}

} // namespace BackendX64
} // namespace Dynarmic

#include "backend_x64/a32_jitstate.h"
#include "backend_x64/a64_jitstate.h"

template class Dynarmic::BackendX64::EmitX64<Dynarmic::BackendX64::A32JitState>;
template class Dynarmic::BackendX64::EmitX64<Dynarmic::BackendX64::A64JitState>;
