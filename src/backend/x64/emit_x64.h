/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/optional.hpp>

#include <xbyak_util.h>

#include "backend/x64/reg_alloc.h"
#include "common/address_range.h"
#include "common/bit_util.h"
#include "common/fp/rounding_mode.h"
#include "frontend/ir/location_descriptor.h"
#include "frontend/ir/terminal.h"

namespace Dynarmic::IR {
class Block;
class Inst;
} // namespace Dynarmic::IR

namespace Dynarmic::BackendX64 {

class BlockOfCode;

using A64FullVectorWidth = std::integral_constant<size_t, 128>;

// Array alias that always sizes itself according to the given type T
// relative to the size of a vector register. e.g. T = u32 would result
// in a std::array<u32, 4>.
template <typename T>
using VectorArray = std::array<T, A64FullVectorWidth::value / Common::BitSize<T>()>;

struct EmitContext {
    EmitContext(RegAlloc& reg_alloc, IR::Block& block);

    void EraseInstruction(IR::Inst* inst);

    virtual FP::RoundingMode FPSCR_RMode() const = 0;
    virtual u32 FPCR() const = 0;
    virtual bool FPSCR_FTZ() const = 0;
    virtual bool FPSCR_DN() const = 0;
    virtual bool AccurateNaN() const { return true; }

    RegAlloc& reg_alloc;
    IR::Block& block;
};

class EmitX64 {
public:
    struct BlockDescriptor {
        CodePtr entrypoint;  // Entrypoint of emitted code
        size_t size;         // Length in bytes of emitted code
    };

    EmitX64(BlockOfCode& code);
    virtual ~EmitX64();

    /// Looks up an emitted host block in the cache.
    boost::optional<BlockDescriptor> GetBasicBlock(IR::LocationDescriptor descriptor) const;

    /// Empties the entire cache.
    virtual void ClearCache();

    /// Invalidates a selection of basic blocks.
    void InvalidateBasicBlocks(const std::unordered_set<IR::LocationDescriptor>& locations);

protected:
    // Microinstruction emitters
#define OPCODE(name, type, ...) void Emit##name(EmitContext& ctx, IR::Inst* inst);
#define A32OPC(...)
#define A64OPC(...)
#include "frontend/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC

    // Helpers
    void EmitAddCycles(size_t cycles);
    Xbyak::Label EmitCond(IR::Cond cond);
    void EmitCondPrelude(const IR::Block& block);
    void PushRSBHelper(Xbyak::Reg64 loc_desc_reg, Xbyak::Reg64 index_reg, IR::LocationDescriptor target);

    // Terminal instruction emitters
    void EmitTerminal(IR::Terminal terminal, IR::LocationDescriptor initial_location);
    virtual void EmitTerminalImpl(IR::Term::Interpret terminal, IR::LocationDescriptor initial_location) = 0;
    virtual void EmitTerminalImpl(IR::Term::ReturnToDispatch terminal, IR::LocationDescriptor initial_location) = 0;
    virtual void EmitTerminalImpl(IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location) = 0;
    virtual void EmitTerminalImpl(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location) = 0;
    virtual void EmitTerminalImpl(IR::Term::PopRSBHint terminal, IR::LocationDescriptor initial_location) = 0;
    virtual void EmitTerminalImpl(IR::Term::If terminal, IR::LocationDescriptor initial_location) = 0;
    virtual void EmitTerminalImpl(IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location) = 0;
    virtual void EmitTerminalImpl(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location) = 0;

    // Patching
    struct PatchInformation {
        std::vector<CodePtr> jg;
        std::vector<CodePtr> jmp;
        std::vector<CodePtr> mov_rcx;
    };
    void Patch(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr);
    void Unpatch(const IR::LocationDescriptor& target_desc);
    virtual void EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr = nullptr) = 0;
    virtual void EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr = nullptr) = 0;
    virtual void EmitPatchMovRcx(CodePtr target_code_ptr = nullptr) = 0;

    // State
    BlockOfCode& code;
    std::unordered_map<IR::LocationDescriptor, BlockDescriptor> block_descriptors;
    std::unordered_map<IR::LocationDescriptor, PatchInformation> patch_information;
};

} // namespace Dynarmic::BackendX64
