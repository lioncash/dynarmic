/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <map>
#include <tuple>

#include "backend/x64/a64_jitstate.h"
#include "backend/x64/block_range_information.h"
#include "backend/x64/emit_x64.h"
#include "dynarmic/A64/a64.h"
#include "dynarmic/A64/config.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/ir/terminal.h"

namespace Dynarmic::BackendX64 {

class RegAlloc;

struct A64EmitContext final : public EmitContext {
    A64EmitContext(const A64::UserConfig& conf, RegAlloc& reg_alloc, IR::Block& block);
    A64::LocationDescriptor Location() const;
    FP::FPCR FPCR() const override;
    bool AccurateNaN() const override;

    const A64::UserConfig& conf;
};

class A64EmitX64 final : public EmitX64 {
public:
    A64EmitX64(BlockOfCode& code, A64::UserConfig conf, A64::Jit* jit_interface);
    ~A64EmitX64() override;

    /**
     * Emit host machine code for a basic block with intermediate representation `ir`.
     * @note ir is modified.
     */
    BlockDescriptor Emit(IR::Block& ir);

    void ClearCache() override;

    void InvalidateCacheRanges(const boost::icl::interval_set<u64>& ranges);

protected:
    const A64::UserConfig conf;
    A64::Jit* jit_interface;
    BlockRangeInformation<u64> block_ranges;

    struct FastDispatchEntry {
        u64 location_descriptor;
        const void* code_ptr;
    };
    static_assert(sizeof(FastDispatchEntry) == 0x10);
    static constexpr u64 fast_dispatch_table_mask = 0xFFFFF0;
    static constexpr size_t fast_dispatch_table_size = 0x100000;
    std::array<FastDispatchEntry, fast_dispatch_table_size> fast_dispatch_table;
    void ClearFastDispatchTable();

    void (*memory_read_128)();
    void (*memory_write_128)();
    void GenMemory128Accessors();

    std::map<std::tuple<size_t, int, int>, void(*)()> read_fallbacks;
    std::map<std::tuple<size_t, int, int>, void(*)()> write_fallbacks;
    void GenFastmemFallbacks();

    const void* terminal_handler_pop_rsb_hint;
    const void* terminal_handler_fast_dispatch_hint = nullptr;
    void GenTerminalHandlers();

    void EmitDirectPageTableMemoryRead(A64EmitContext& ctx, IR::Inst* inst, size_t bitsize);
    void EmitDirectPageTableMemoryWrite(A64EmitContext& ctx, IR::Inst* inst, size_t bitsize);
    void EmitExclusiveWrite(A64EmitContext& ctx, IR::Inst* inst, size_t bitsize);

    // Microinstruction emitters
#define OPCODE(...)
#define A32OPC(...)
#define A64OPC(name, type, ...) void EmitA64##name(A64EmitContext& ctx, IR::Inst* inst);
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
