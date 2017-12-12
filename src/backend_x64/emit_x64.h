/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <unordered_map>
#include <vector>

#include <boost/icl/interval_map.hpp>
#include <boost/icl/interval_set.hpp>
#include <boost/optional.hpp>

#include <xbyak_util.h>

#include "backend_x64/reg_alloc.h"
#include "common/address_range.h"
#include "dynarmic/callbacks.h"
#include "frontend/ir/location_descriptor.h"
#include "frontend/ir/terminal.h"

namespace Dynarmic {

class Jit;

namespace IR {
class Block;
class Inst;
} // namespace IR

namespace BackendX64 {

class BlockOfCode;

class EmitX64 final {
public:
    struct BlockDescriptor {
        CodePtr entrypoint;  // Entrypoint of emitted code
        size_t size;         // Length in bytes of emitted code

        IR::LocationDescriptor start_location;
        u32 end_location_pc;
    };

    EmitX64(BlockOfCode* code, UserCallbacks cb, Jit* jit_interface);

    /**
     * Emit host machine code for a basic block with intermediate representation `ir`.
     * @note ir is modified.
     */
    BlockDescriptor Emit(IR::Block& ir);

    /// Looks up an emitted host block in the cache.
    boost::optional<BlockDescriptor> GetBasicBlock(IR::LocationDescriptor descriptor) const;

    /// Empties the entire cache.
    void ClearCache();

    /**
     * Invalidate the cache at a set of ranges of addresses.
     * @param ranges The set of ranges of addresses to invalidate the cache at.
     */
    void InvalidateCacheRanges(const boost::icl::interval_set<u32>& ranges);

private:
    // Microinstruction emitters
#define OPCODE(name, type, ...) void Emit##name(RegAlloc& reg_alloc, IR::Block& block, IR::Inst* inst);
#include "frontend/ir/opcodes.inc"
#undef OPCODE

    // Helpers
    void EmitAddCycles(size_t cycles);
    void EmitCondPrelude(const IR::Block& block);
    void PushRSBHelper(Xbyak::Reg64 loc_desc_reg, Xbyak::Reg64 index_reg, u64 target_hash);

    // Terminal instruction emitters
    void EmitTerminal(IR::Terminal terminal, IR::LocationDescriptor initial_location);
    void EmitTerminal(IR::Term::Interpret terminal, IR::LocationDescriptor initial_location);
    void EmitTerminal(IR::Term::ReturnToDispatch terminal, IR::LocationDescriptor initial_location);
    void EmitTerminal(IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location);
    void EmitTerminal(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location);
    void EmitTerminal(IR::Term::PopRSBHint terminal, IR::LocationDescriptor initial_location);
    void EmitTerminal(IR::Term::If terminal, IR::LocationDescriptor initial_location);
    void EmitTerminal(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location);

    // Patching
    struct PatchInformation {
        std::vector<CodePtr> jg;
        std::vector<CodePtr> jmp;
        std::vector<CodePtr> mov_rcx;
    };
    void Patch(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr);
    void Unpatch(const IR::LocationDescriptor& target_desc);
    void EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr = nullptr);
    void EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr = nullptr);
    void EmitPatchMovRcx(CodePtr target_code_ptr = nullptr);

    // State
    BlockOfCode* code;
    UserCallbacks cb;
    boost::icl::interval_map<u32, std::set<IR::LocationDescriptor>> block_ranges;
    Jit* jit_interface;
    std::unordered_map<u64, BlockDescriptor> block_descriptors;
    std::unordered_map<u64, PatchInformation> patch_information;
};

} // namespace BackendX64
} // namespace Dynarmic
