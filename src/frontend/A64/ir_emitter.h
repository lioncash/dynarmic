/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <initializer_list>
#include <optional>

#include <dynarmic/A64/config.h>

#include "common/common_types.h"
#include "frontend/A64/location_descriptor.h"
#include "frontend/A64/types.h"
#include "frontend/ir/ir_emitter.h"
#include "frontend/ir/value.h"

namespace Dynarmic::A64 {

/**
 * Convenience class to construct a basic block of the intermediate representation.
 * `block` is the resulting block.
 * The user of this class updates `current_location` as appropriate.
 */
class IREmitter : public IR::IREmitter {
public:
    explicit IREmitter(IR::Block& block) : IR::IREmitter(block) {}
    explicit IREmitter(IR::Block& block, LocationDescriptor descriptor) : IR::IREmitter(block), current_location(descriptor) {}

    std::optional<LocationDescriptor> current_location;

    u64 PC();
    u64 AlignPC(size_t alignment);

    void SetCheckBit(const IR::U1& value);
    IR::U1 GetCFlag();
    void SetNZCV(const IR::NZCV& nzcv);
    void OrQC(const IR::U1& value);

    void CallSupervisor(u32 imm);
    void ExceptionRaised(Exception exception);
    void DataCacheOperationRaised(DataCacheOperation op, const IR::U64& value);
    void DataSynchronizationBarrier();
    void DataMemoryBarrier();
    void InstructionSynchronizationBarrier();
    IR::U32 GetCNTFRQ();
    IR::U64 GetCNTPCT(); // TODO: Ensure sub-basic-block cycle counts are updated before this.
    IR::U32 GetCTR();
    IR::U32 GetDCZID();
    IR::U64 GetTPIDR();
    IR::U64 GetTPIDRRO();
    void SetTPIDR(const IR::U64& value);

    void ClearExclusive();
    void SetExclusive(const IR::U64& vaddr, size_t byte_size);
    IR::U8 ReadMemory8(const IR::U64& vaddr);
    IR::U16 ReadMemory16(const IR::U64& vaddr);
    IR::U32 ReadMemory32(const IR::U64& vaddr);
    IR::U64 ReadMemory64(const IR::U64& vaddr);
    IR::U128 ReadMemory128(const IR::U64& vaddr);
    void WriteMemory8(const IR::U64& vaddr, const IR::U8& value);
    void WriteMemory16(const IR::U64& vaddr, const IR::U16& value);
    void WriteMemory32(const IR::U64& vaddr, const IR::U32& value);
    void WriteMemory64(const IR::U64& vaddr, const IR::U64& value);
    void WriteMemory128(const IR::U64& vaddr, const IR::U128& value);
    IR::U32 ExclusiveWriteMemory8(const IR::U64& vaddr, const IR::U8& value);
    IR::U32 ExclusiveWriteMemory16(const IR::U64& vaddr, const IR::U16& value);
    IR::U32 ExclusiveWriteMemory32(const IR::U64& vaddr, const IR::U32& value);
    IR::U32 ExclusiveWriteMemory64(const IR::U64& vaddr, const IR::U64& value);
    IR::U32 ExclusiveWriteMemory128(const IR::U64& vaddr, const IR::U128& value);

    IR::U32 GetW(Reg source_reg);
    IR::U64 GetX(Reg source_reg);
    IR::U128 GetS(Vec source_vec);
    IR::U128 GetD(Vec source_vec);
    IR::U128 GetQ(Vec source_vec);
    IR::U64 GetSP();
    IR::U32 GetFPCR();
    IR::U32 GetFPSR();
    void SetW(Reg dest_reg, const IR::U32& value);
    void SetX(Reg dest_reg, const IR::U64& value);
    void SetS(Vec dest_vec, const IR::U128& value);
    void SetD(Vec dest_vec, const IR::U128& value);
    void SetQ(Vec dest_vec, const IR::U128& value);
    void SetSP(const IR::U64& value);
    void SetFPCR(const IR::U32& value);
    void SetFPSR(const IR::U32& value);
    void SetPC(const IR::U64& value);
};

} // namespace Dynarmic::A64
