/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#pragma once

#include <initializer_list>

#include <dynarmic/A32/coprocessor_util.h>

#include "common/common_types.h"
#include "frontend/A32/location_descriptor.h"
#include "frontend/A32/types.h"
#include "frontend/ir/ir_emitter.h"
#include "frontend/ir/value.h"

namespace Dynarmic {
namespace A32 {

/**
 * Convenience class to construct a basic block of the intermediate representation.
 * `block` is the resulting block.
 * The user of this class updates `current_location` as appropriate.
 */
class IREmitter : public IR::IREmitter {
public:
    explicit IREmitter(A32::LocationDescriptor descriptor) : IR::IREmitter(descriptor), current_location(descriptor) {}

    A32::LocationDescriptor current_location;

    u32 PC();
    u32 AlignPC(size_t alignment);

    IR::Value GetRegister(A32::Reg source_reg);
    IR::Value GetExtendedRegister(A32::ExtReg source_reg);
    void SetRegister(const A32::Reg dest_reg, const IR::Value& value);
    void SetExtendedRegister(const A32::ExtReg dest_reg, const IR::Value& value);

    void ALUWritePC(const IR::Value& value);
    void BranchWritePC(const IR::Value& value);
    void BXWritePC(const IR::Value& value);
    void LoadWritePC(const IR::Value& value);
    void CallSupervisor(const IR::Value& value);

    IR::Value GetCpsr();
    void SetCpsr(const IR::Value& value);
    void SetCpsrNZCV(const IR::Value& value);
    void SetCpsrNZCVQ(const IR::Value& value);
    IR::Value GetCFlag();
    void SetNFlag(const IR::Value& value);
    void SetZFlag(const IR::Value& value);
    void SetCFlag(const IR::Value& value);
    void SetVFlag(const IR::Value& value);
    void OrQFlag(const IR::Value& value);
    IR::Value GetGEFlags();
    void SetGEFlags(const IR::Value& value);
    void SetGEFlagsCompressed(const IR::Value& value);

    IR::Value GetFpscr();
    void SetFpscr(const IR::Value& new_fpscr);
    IR::Value GetFpscrNZCV();
    void SetFpscrNZCV(const IR::Value& new_fpscr_nzcv);

    void ClearExclusive();
    void SetExclusive(const IR::Value& vaddr, size_t byte_size);
    IR::Value ReadMemory8(const IR::Value& vaddr);
    IR::Value ReadMemory16(const IR::Value& vaddr);
    IR::Value ReadMemory32(const IR::Value& vaddr);
    IR::Value ReadMemory64(const IR::Value& vaddr);
    void WriteMemory8(const IR::Value& vaddr, const IR::Value& value);
    void WriteMemory16(const IR::Value& vaddr, const IR::Value& value);
    void WriteMemory32(const IR::Value& vaddr, const IR::Value& value);
    void WriteMemory64(const IR::Value& vaddr, const IR::Value& value);
    IR::Value ExclusiveWriteMemory8(const IR::Value& vaddr, const IR::Value& value);
    IR::Value ExclusiveWriteMemory16(const IR::Value& vaddr, const IR::Value& value);
    IR::Value ExclusiveWriteMemory32(const IR::Value& vaddr, const IR::Value& value);
    IR::Value ExclusiveWriteMemory64(const IR::Value& vaddr, const IR::Value& value_lo, const IR::Value& value_hi);

    void CoprocInternalOperation(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRd, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2);
    void CoprocSendOneWord(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2, const IR::Value& word);
    void CoprocSendTwoWords(size_t coproc_no, bool two, size_t opc, A32::CoprocReg CRm, const IR::Value& word1, const IR::Value& word2);
    IR::Value CoprocGetOneWord(size_t coproc_no, bool two, size_t opc1, A32::CoprocReg CRn, A32::CoprocReg CRm, size_t opc2);
    IR::Value CoprocGetTwoWords(size_t coproc_no, bool two, size_t opc, A32::CoprocReg CRm);
    void CoprocLoadWords(size_t coproc_no, bool two, bool long_transfer, A32::CoprocReg CRd, const IR::Value& address, bool has_option, u8 option);
    void CoprocStoreWords(size_t coproc_no, bool two, bool long_transfer, A32::CoprocReg CRd, const IR::Value& address, bool has_option, u8 option);
};

} // namespace IR
} // namespace Dynarmic
