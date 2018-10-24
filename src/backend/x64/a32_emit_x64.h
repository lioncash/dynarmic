/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <array>
#include <optional>

#include "backend/x64/a32_jitstate.h"
#include "backend/x64/block_range_information.h"
#include "backend/x64/emit_x64.h"
#include "dynarmic/A32/a32.h"
#include "dynarmic/A32/config.h"
#include "frontend/A32/location_descriptor.h"
#include "frontend/ir/terminal.h"

namespace Dynarmic::BackendX64 {

class RegAlloc;

struct A32EmitContext final : public EmitContext {
    A32EmitContext(RegAlloc& reg_alloc, IR::Block& block);
    A32::LocationDescriptor Location() const;
    FP::RoundingMode FPSCR_RMode() const override;
    u32 FPCR() const override;
    bool FPSCR_FTZ() const override;
    bool FPSCR_DN() const override;
};

class A32EmitX64 final : public EmitX64 {
public:
    A32EmitX64(BlockOfCode& code, A32::UserConfig config, A32::Jit* jit_interface);
    ~A32EmitX64() override;

    /**
     * Emit host machine code for a basic block with intermediate representation `ir`.
     * @note ir is modified.
     */
    BlockDescriptor Emit(IR::Block& ir);

    void ClearCache() override;

    void InvalidateCacheRanges(const boost::icl::interval_set<u32>& ranges);

protected:
    const A32::UserConfig config;
    A32::Jit* jit_interface;
    BlockRangeInformation<u32> block_ranges;

    struct FastDispatchEntry {
        u64 location_descriptor;
        const void* code_ptr;
    };
    static_assert(sizeof(FastDispatchEntry) == 0x10);
    static constexpr u64 fast_dispatch_table_mask = 0xFFFF0;
    static constexpr size_t fast_dispatch_table_size = 0x10000;
    std::array<FastDispatchEntry, fast_dispatch_table_size> fast_dispatch_table;
    void ClearFastDispatchTable();

    const void* read_memory_8;
    const void* read_memory_16;
    const void* read_memory_32;
    const void* read_memory_64;
    const void* write_memory_8;
    const void* write_memory_16;
    const void* write_memory_32;
    const void* write_memory_64;
    void GenMemoryAccessors();

    const void* terminal_handler_pop_rsb_hint;
    const void* terminal_handler_fast_dispatch_hint = nullptr;
    void GenTerminalHandlers();

    // Microinstruction emitters
#define OPCODE(...)
#define A32OPC(name, type, ...) void EmitA32##name(A32EmitContext& ctx, IR::Inst* inst);
#define A64OPC(...)
#include "frontend/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC

    // Helpers
    std::string LocationDescriptorToFriendlyName(const IR::LocationDescriptor&) const override;

    // Terminal instruction emitters
    void EmitTerminalImpl(IR::Term::Interpret terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::ReturnToDispatch terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::PopRSBHint terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::FastDispatchHint terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::If terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location) override;
    void EmitTerminalImpl(IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location) override;

    // Patching
    void EmitPatchJg(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr = nullptr) override;
    void EmitPatchJmp(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr = nullptr) override;
    void EmitPatchMovRcx(CodePtr target_code_ptr = nullptr) override;
};

} // namespace Dynarmic::BackendX64
