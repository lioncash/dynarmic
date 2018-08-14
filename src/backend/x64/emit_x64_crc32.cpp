/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public icense version 2 or any later version.
 */

#include <array>
#include <climits>

#include "backend/x64/block_of_code.h"
#include "backend/x64/emit_x64.h"
#include "common/common_types.h"
#include "common/crypto/crc32.h"
#include "frontend/ir/microinstruction.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic::BackendX64 {

using namespace Xbyak::util;
namespace CRC32 = Common::Crypto::CRC32;

static void EmitCRC32Castagnoli(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, const int data_size) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (code.DoesCpuSupport(Xbyak::util::Cpu::tSSE42)) {
        const Xbyak::Reg32 crc = ctx.reg_alloc.UseScratchGpr(args[0]).cvt32();
        const Xbyak::Reg value = ctx.reg_alloc.UseGpr(args[1]).changeBit(data_size);
        code.crc32(crc, value);
        ctx.reg_alloc.DefineValue(inst, crc);
    } else {
        ctx.reg_alloc.HostCall(inst, args[0], args[1], {});
        code.mov(code.ABI_PARAM3, data_size / CHAR_BIT);
        code.CallFunction(&CRC32::ComputeCRC32Castagnoli);
    }
}

static void EmitCRC32ISO(BlockOfCode& code, EmitContext& ctx, IR::Inst* inst, const int data_size) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.HostCall(inst, args[0], args[1], {});
    code.mov(code.ABI_PARAM3, data_size / CHAR_BIT);
    code.CallFunction(&CRC32::ComputeCRC32ISO);
}

void EmitX64::EmitCRC32Castagnoli8(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32Castagnoli(code, ctx, inst, 8);
}

void EmitX64::EmitCRC32Castagnoli16(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32Castagnoli(code, ctx, inst, 16);
}

void EmitX64::EmitCRC32Castagnoli32(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32Castagnoli(code, ctx, inst, 32);
}

void EmitX64::EmitCRC32Castagnoli64(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32Castagnoli(code, ctx, inst, 64);
}

void EmitX64::EmitCRC32ISO8(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32ISO(code, ctx, inst, 8);
}

void EmitX64::EmitCRC32ISO16(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32ISO(code, ctx, inst, 16);
}

void EmitX64::EmitCRC32ISO32(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32ISO(code, ctx, inst, 32);
}

void EmitX64::EmitCRC32ISO64(EmitContext& ctx, IR::Inst* inst) {
    EmitCRC32ISO(code, ctx, inst, 64);
}

} // namespace Dynarmic::BackendX64
