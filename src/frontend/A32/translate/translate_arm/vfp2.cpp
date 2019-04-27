/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include "translate_arm.h"

namespace Dynarmic::A32 {

static ExtReg ToExtReg(bool sz, size_t base, bool bit) {
    if (sz) {
        return static_cast<ExtReg>(static_cast<size_t>(ExtReg::D0) + base + (bit ? 16 : 0));
    } else {
        return static_cast<ExtReg>(static_cast<size_t>(ExtReg::S0) + (base << 1) + (bit ? 1 : 0));
    }
}

template <typename FnT>
bool ArmTranslatorVisitor::EmitVfpVectorOperation(bool sz, ExtReg d, ExtReg n, ExtReg m, const FnT& fn) {
    if (!ir.current_location.FPSCR().Stride()) {
        return UnpredictableInstruction();
    }

    // VFP register banks are 8 single-precision registers in size.
    const size_t register_bank_size = sz ? 4 : 8;
    size_t vector_length = ir.current_location.FPSCR().Len();
    const size_t vector_stride = *ir.current_location.FPSCR().Stride();

    // Unpredictable case
    if (vector_stride * vector_length > register_bank_size) {
        return UnpredictableInstruction();
    }

    // Scalar case
    if (vector_length == 1) {
        if (vector_stride != 1) {
            return UnpredictableInstruction();
        }

        fn(d, n, m);
        return true;
    }

    // The VFP register file is divided into banks each containing:
    // * eight single-precision registers, or
    // * four double-precision reigsters.
    // VFP vector instructions access these registers in a circular manner.
    const auto bank_increment = [register_bank_size](ExtReg reg, size_t stride) -> ExtReg {
        const auto reg_number = static_cast<size_t>(reg);
        const auto bank_index = reg_number % register_bank_size;
        const auto bank_start = reg_number - bank_index;
        const auto next_reg_number = bank_start + ((bank_index + stride) % register_bank_size);
        return static_cast<ExtReg>(next_reg_number);
    };

    // The first and fifth banks in the register file are scalar banks.
    // All the other banks are vector banks.
    const auto belongs_to_scalar_bank = [](ExtReg reg) -> bool {
        return (reg >= ExtReg::D0 && reg <= ExtReg::D3)
            || (reg >= ExtReg::D16 && reg <= ExtReg::D19)
            || (reg >= ExtReg::S0 && reg <= ExtReg::S7);
    };

    const bool d_is_scalar = belongs_to_scalar_bank(d);
    const bool m_is_scalar = belongs_to_scalar_bank(m);

    if (d_is_scalar) {
        // If destination register is in a scalar bank, the operands and results are all scalars.
        vector_length = 1;
    }

    for (size_t i = 0; i < vector_length; i++) {
        fn(d, n, m);

        d = bank_increment(d, vector_stride);
        n = bank_increment(n, vector_stride);
        if (!m_is_scalar) {
            m = bank_increment(m, vector_stride);
        }
    }

    return true;
}

template <typename FnT>
bool ArmTranslatorVisitor::EmitVfpVectorOperation(bool sz, ExtReg d, ExtReg m, const FnT& fn) {
    return EmitVfpVectorOperation(sz, d, ExtReg::S0, m, [fn](ExtReg d, ExtReg, ExtReg m){
        fn(d, m);
    });
}

// VADD<c>.F64 <Dd>, <Dn>, <Dm>
// VADD<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VADD(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPAdd(reg_n, reg_m, true);
        ir.SetExtendedRegister(d, result);
    });
}

// VSUB<c>.F64 <Dd>, <Dn>, <Dm>
// VSUB<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VSUB(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPSub(reg_n, reg_m, true);
        ir.SetExtendedRegister(d, result);
    });
}

// VMUL<c>.F64 <Dd>, <Dn>, <Dm>
// VMUL<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VMUL(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPMul(reg_n, reg_m, true);
        ir.SetExtendedRegister(d, result);
    });
}

// VMLA<c>.F64 <Dd>, <Dn>, <Dm>
// VMLA<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VMLA(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto reg_d = ir.GetExtendedRegister(d);
        const auto result = ir.FPAdd(reg_d, ir.FPMul(reg_n, reg_m, true), true);
        ir.SetExtendedRegister(d, result);
    });
}

// VMLS<c>.F64 <Dd>, <Dn>, <Dm>
// VMLS<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VMLS(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto reg_d = ir.GetExtendedRegister(d);
        const auto result = ir.FPAdd(reg_d, ir.FPNeg(ir.FPMul(reg_n, reg_m, true)), true);
        ir.SetExtendedRegister(d, result);
    });
}

// VNMUL<c>.F64 <Dd>, <Dn>, <Dm>
// VNMUL<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VNMUL(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPNeg(ir.FPMul(reg_n, reg_m, true));
        ir.SetExtendedRegister(d, result);
    });
}

// VNMLA<c>.F64 <Dd>, <Dn>, <Dm>
// VNMLA<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VNMLA(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto reg_d = ir.GetExtendedRegister(d);
        const auto result = ir.FPAdd(ir.FPNeg(reg_d), ir.FPNeg(ir.FPMul(reg_n, reg_m, true)), true);
        ir.SetExtendedRegister(d, result);
    });
}

// VNMLS<c>.F64 <Dd>, <Dn>, <Dm>
// VNMLS<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VNMLS(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto reg_d = ir.GetExtendedRegister(d);
        const auto result = ir.FPAdd(ir.FPNeg(reg_d), ir.FPMul(reg_n, reg_m, true), true);
        ir.SetExtendedRegister(d, result);
    });
}

// VDIV<c>.F64 <Dd>, <Dn>, <Dm>
// VDIV<c>.F32 <Sd>, <Sn>, <Sm>
bool ArmTranslatorVisitor::vfp2_VDIV(Cond cond, bool D, size_t Vn, size_t Vd, bool sz, bool N, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto n = ToExtReg(sz, Vn, N);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, n, m, [this](ExtReg d, ExtReg n, ExtReg m) {
        const auto reg_n = ir.GetExtendedRegister(n);
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPDiv(reg_n, reg_m, true);
        ir.SetExtendedRegister(d, result);
    });
}

// VMOV<c>.32 <Dd[0]>, <Rt>
bool ArmTranslatorVisitor::vfp2_VMOV_u32_f64(Cond cond, size_t Vd, Reg t, bool D) {
    if (t == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(true, Vd, D);
    const auto reg_d = ir.GetExtendedRegister(d);
    const auto reg_t = ir.GetRegister(t);
    const auto result = ir.Pack2x32To1x64(reg_t, ir.MostSignificantWord(reg_d).result);

    ir.SetExtendedRegister(d, result);
    return true;
}

// VMOV<c>.32 <Rt>, <Dn[0]>
bool ArmTranslatorVisitor::vfp2_VMOV_f64_u32(Cond cond, size_t Vn, Reg t, bool N) {
    if (t == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto n = ToExtReg(true, Vn, N);
    const auto reg_n = ir.GetExtendedRegister(n);
    ir.SetRegister(t, ir.LeastSignificantWord(reg_n));
    return true;
}

// VMOV<c> <Sn>, <Rt>
bool ArmTranslatorVisitor::vfp2_VMOV_u32_f32(Cond cond, size_t Vn, Reg t, bool N) {
    if (t == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto n = ToExtReg(false, Vn, N);
    ir.SetExtendedRegister(n, ir.GetRegister(t));
    return true;
}

// VMOV<c> <Rt>, <Sn>
bool ArmTranslatorVisitor::vfp2_VMOV_f32_u32(Cond cond, size_t Vn, Reg t, bool N) {
    if (t == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto n = ToExtReg(false, Vn, N);
    ir.SetRegister(t, ir.GetExtendedRegister(n));
    return true;
}

// VMOV<c> <Sm>, <Sm1>, <Rt>, <Rt2>
bool ArmTranslatorVisitor::vfp2_VMOV_2u32_2f32(Cond cond, Reg t2, Reg t, bool M, size_t Vm) {
    const auto m = ToExtReg(false, Vm, M);
    if (t == Reg::PC || t2 == Reg::PC || m == ExtReg::S31) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    ir.SetExtendedRegister(m, ir.GetRegister(t));
    ir.SetExtendedRegister(m+1, ir.GetRegister(t2));
    return true;
}

// VMOV<c> <Rt>, <Rt2>, <Sm>, <Sm1>
bool ArmTranslatorVisitor::vfp2_VMOV_2f32_2u32(Cond cond, Reg t2, Reg t, bool M, size_t Vm) {
    const auto m = ToExtReg(false, Vm, M);
    if (t == Reg::PC || t2 == Reg::PC || m == ExtReg::S31) {
        return UnpredictableInstruction();
    }

    if (t == t2) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    ir.SetRegister(t, ir.GetExtendedRegister(m));
    ir.SetRegister(t2, ir.GetExtendedRegister(m+1));
    return true;
}

// VMOV<c> <Dm>, <Rt>, <Rt2>
bool ArmTranslatorVisitor::vfp2_VMOV_2u32_f64(Cond cond, Reg t2, Reg t, bool M, size_t Vm) {
    const auto m = ToExtReg(true, Vm, M);
    if (t == Reg::PC || t2 == Reg::PC || m == ExtReg::S31) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto value = ir.Pack2x32To1x64(ir.GetRegister(t), ir.GetRegister(t2));
    ir.SetExtendedRegister(m, value);
    return true;
}

// VMOV<c> <Rt>, <Rt2>, <Dm>
bool ArmTranslatorVisitor::vfp2_VMOV_f64_2u32(Cond cond, Reg t2, Reg t, bool M, size_t Vm) {
    const auto m = ToExtReg(true, Vm, M);
    if (t == Reg::PC || t2 == Reg::PC || m == ExtReg::S31) {
        return UnpredictableInstruction();
    }

    if (t == t2) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto value = ir.GetExtendedRegister(m);
    ir.SetRegister(t, ir.LeastSignificantWord(value));
    ir.SetRegister(t2, ir.MostSignificantWord(value).result);
    return true;
}

// VMOV<c>.F64 <Dd>, <Dm>
// VMOV<c>.F32 <Sd>, <Sm>
bool ArmTranslatorVisitor::vfp2_VMOV_reg(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, m, [this](ExtReg d, ExtReg m) {
        ir.SetExtendedRegister(d, ir.GetExtendedRegister(m));
    });
}

// VABS<c>.F64 <Dd>, <Dm>
// VABS<c>.F32 <Sd>, <Sm>
bool ArmTranslatorVisitor::vfp2_VABS(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, m, [this](ExtReg d, ExtReg m) {
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPAbs(reg_m);
        ir.SetExtendedRegister(d, result);
    });
}

// VNEG<c>.F64 <Dd>, <Dm>
// VNEG<c>.F32 <Sd>, <Sm>
bool ArmTranslatorVisitor::vfp2_VNEG(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, m, [this](ExtReg d, ExtReg m) {
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPNeg(reg_m);
        ir.SetExtendedRegister(d, result);
    });
}

// VSQRT<c>.F64 <Dd>, <Dm>
// VSQRT<c>.F32 <Sd>, <Sm>
bool ArmTranslatorVisitor::vfp2_VSQRT(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto m = ToExtReg(sz, Vm, M);

    return EmitVfpVectorOperation(sz, d, m, [this](ExtReg d, ExtReg m) {
        const auto reg_m = ir.GetExtendedRegister(m);
        const auto result = ir.FPSqrt(reg_m);
        ir.SetExtendedRegister(d, result);
    });
}

// VCVT<c>.F64.F32 <Dd>, <Sm>
// VCVT<c>.F32.F64 <Sd>, <Dm>
bool ArmTranslatorVisitor::vfp2_VCVT_f_to_f(Cond cond, bool D, size_t Vd, bool sz, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(!sz, Vd, D); // Destination is of opposite size to source
    const auto m = ToExtReg(sz, Vm, M);
    const auto reg_m = ir.GetExtendedRegister(m);
    const auto rounding_mode = ir.current_location.FPSCR().RMode();

    if (sz) {
        const auto result = ir.FPDoubleToSingle(reg_m, rounding_mode);
        ir.SetExtendedRegister(d, result);
    } else {
        const auto result = ir.FPSingleToDouble(reg_m, rounding_mode);
        ir.SetExtendedRegister(d, result);
    }

    return true;
}

// VCVT.F32.{S32,U32} <Sd>, <Sm>
// VCVT.F64.{S32,U32} <Sd>, <Dm>
bool ArmTranslatorVisitor::vfp2_VCVT_to_float(Cond cond, bool D, size_t Vd, bool sz, bool is_signed, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto m = ToExtReg(false, Vm, M);
    const auto rounding_mode = ir.current_location.FPSCR().RMode();
    const auto reg_m = ir.GetExtendedRegister(m);

    if (sz) {
        const auto result = is_signed
                          ? ir.FPSignedFixedToDouble(reg_m, 0, rounding_mode)
                          : ir.FPUnsignedFixedToDouble(reg_m, 0, rounding_mode);
        ir.SetExtendedRegister(d, result);
    } else {
        const auto result = is_signed
                          ? ir.FPSignedFixedToSingle(reg_m, 0, rounding_mode)
                          : ir.FPUnsignedFixedToSingle(reg_m, 0, rounding_mode);
        ir.SetExtendedRegister(d, result);
    }

    return true;
}

// VCVT{,R}.U32.F32 <Sd>, <Sm>
// VCVT{,R}.U32.F64 <Sd>, <Dm>
bool ArmTranslatorVisitor::vfp2_VCVT_to_u32(Cond cond, bool D, size_t Vd, bool sz, bool round_towards_zero, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const ExtReg d = ToExtReg(false, Vd, D);
    const ExtReg m = ToExtReg(sz, Vm, M);
    const auto reg_m = ir.GetExtendedRegister(m);
    const auto result = ir.FPToFixedU32(reg_m, 0, round_towards_zero ? FP::RoundingMode::TowardsZero : ir.current_location.FPSCR().RMode());

    ir.SetExtendedRegister(d, result);
    return true;
}

// VCVT{,R}.S32.F32 <Sd>, <Sm>
// VCVT{,R}.S32.F64 <Sd>, <Dm>
bool ArmTranslatorVisitor::vfp2_VCVT_to_s32(Cond cond, bool D, size_t Vd, bool sz, bool round_towards_zero, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(false, Vd, D);
    const auto m = ToExtReg(sz, Vm, M);
    const auto reg_m = ir.GetExtendedRegister(m);
    const auto result = ir.FPToFixedS32(reg_m, 0, round_towards_zero ? FP::RoundingMode::TowardsZero : ir.current_location.FPSCR().RMode());

    ir.SetExtendedRegister(d, result);
    return true;
}

// VCMP{E}.F32 <Sd>, <Sm>
// VCMP{E}.F64 <Dd>, <Dm>
bool ArmTranslatorVisitor::vfp2_VCMP(Cond cond, bool D, size_t Vd, bool sz, bool E, bool M, size_t Vm) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto m = ToExtReg(sz, Vm, M);
    const auto exc_on_qnan = E;
    const auto reg_d = ir.GetExtendedRegister(d);
    const auto reg_m = ir.GetExtendedRegister(m);
    const auto nzcv = ir.FPCompare(reg_d, reg_m, exc_on_qnan, true);

    ir.SetFpscrNZCV(nzcv);
    return true;
}

// VCMP{E}.F32 <Sd>, #0.0
// VCMP{E}.F64 <Dd>, #0.0
bool ArmTranslatorVisitor::vfp2_VCMP_zero(Cond cond, bool D, size_t Vd, bool sz, bool E) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const auto d = ToExtReg(sz, Vd, D);
    const auto exc_on_qnan = E;
    const auto reg_d = ir.GetExtendedRegister(d);

    if (sz) {
        const auto nzcv = ir.FPCompare(reg_d, ir.Imm64(0), exc_on_qnan, true);
        ir.SetFpscrNZCV(nzcv);
    } else {
        const auto nzcv = ir.FPCompare(reg_d, ir.Imm32(0), exc_on_qnan, true);
        ir.SetFpscrNZCV(nzcv);
    }

    return true;
}

// VMSR FPSCR, <Rt>
bool ArmTranslatorVisitor::vfp2_VMSR(Cond cond, Reg t) {
    if (t == Reg::PC) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    // TODO: Replace this with a local cache.
    ir.PushRSB(ir.current_location.AdvancePC(4));

    ir.SetFpscr(ir.GetRegister(t));
    ir.BranchWritePC(ir.Imm32(ir.current_location.PC() + 4));
    ir.SetTerm(IR::Term::PopRSBHint{});
    return false;
}

// VMRS <Rt>, FPSCR
bool ArmTranslatorVisitor::vfp2_VMRS(Cond cond, Reg t) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    if (t == Reg::R15) {
        // This encodes ASPR_nzcv access
        const auto nzcv = ir.GetFpscrNZCV();
        ir.SetCpsrNZCV(nzcv);
    } else {
        ir.SetRegister(t, ir.GetFpscr());
    }

    return true;
}

// VPOP.{F32,F64} <list>
bool ArmTranslatorVisitor::vfp2_VPOP(Cond cond, bool D, size_t Vd, bool sz, Imm<8> imm8) {
    const ExtReg d = ToExtReg(sz, Vd, D);
    const size_t regs = sz ? imm8.ZeroExtend() >> 1 : imm8.ZeroExtend();

    if (regs == 0 || RegNumber(d)+regs > 32) {
        return UnpredictableInstruction();
    }

    if (sz && regs > 16) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    auto address = ir.GetRegister(Reg::SP);

    for (size_t i = 0; i < regs; ++i) {
        if (sz) {
            auto lo = ir.ReadMemory32(address);
            address = ir.Add(address, ir.Imm32(4));
            auto hi = ir.ReadMemory32(address);
            address = ir.Add(address, ir.Imm32(4));
            if (ir.current_location.EFlag()) {
                std::swap(lo, hi);
            }
            ir.SetExtendedRegister(d + i, ir.Pack2x32To1x64(lo, hi));
        } else {
            const auto res = ir.ReadMemory32(address);
            ir.SetExtendedRegister(d + i, res);
            address = ir.Add(address, ir.Imm32(4));
        }
    }

    ir.SetRegister(Reg::SP, address);
    return true;
}

// VPUSH.{F32,F64} <list>
bool ArmTranslatorVisitor::vfp2_VPUSH(Cond cond, bool D, size_t Vd, bool sz, Imm<8> imm8) {
    const ExtReg d = ToExtReg(sz, Vd, D);
    const size_t regs = sz ? imm8.ZeroExtend() >> 1 : imm8.ZeroExtend();

    if (regs == 0 || RegNumber(d)+regs > 32) {
        return UnpredictableInstruction();
    }

    if (sz && regs > 16) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm8.ZeroExtend() << 2;
    auto address = ir.Sub(ir.GetRegister(Reg::SP), ir.Imm32(imm32));
    ir.SetRegister(Reg::SP, address);

    for (size_t i = 0; i < regs; ++i) {
        if (sz) {
            const auto reg_d = ir.GetExtendedRegister(d + i);
            auto lo = ir.LeastSignificantWord(reg_d);
            auto hi = ir.MostSignificantWord(reg_d).result;
            if (ir.current_location.EFlag()) std::swap(lo, hi);
            ir.WriteMemory32(address, lo);
            address = ir.Add(address, ir.Imm32(4));
            ir.WriteMemory32(address, hi);
            address = ir.Add(address, ir.Imm32(4));
        } else {
            ir.WriteMemory32(address, ir.GetExtendedRegister(d + i));
            address = ir.Add(address, ir.Imm32(4));
        }
    }

    return true;
}

// VLDR<c> <Dd>, [<Rn>{, #+/-<imm>}]
// VLDR<c> <Sd>, [<Rn>{, #+/-<imm>}]
bool ArmTranslatorVisitor::vfp2_VLDR(Cond cond, bool U, bool D, Reg n, size_t Vd, bool sz, Imm<8> imm8) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm8.ZeroExtend() << 2;
    const auto d = ToExtReg(sz, Vd, D);
    const auto base = n == Reg::PC ? ir.Imm32(ir.AlignPC(4)) : ir.GetRegister(n);
    const auto address = U ? ir.Add(base, ir.Imm32(imm32)) : ir.Sub(base, ir.Imm32(imm32));

    if (sz) {
        auto lo = ir.ReadMemory32(address);
        auto hi = ir.ReadMemory32(ir.Add(address, ir.Imm32(4)));
        if (ir.current_location.EFlag()) {
            std::swap(lo, hi);
        }
        ir.SetExtendedRegister(d, ir.Pack2x32To1x64(lo, hi));
    } else {
        ir.SetExtendedRegister(d, ir.ReadMemory32(address));
    }

    return true;
}

// VSTR<c> <Dd>, [<Rn>{, #+/-<imm>}]
// VSTR<c> <Sd>, [<Rn>{, #+/-<imm>}]
bool ArmTranslatorVisitor::vfp2_VSTR(Cond cond, bool U, bool D, Reg n, size_t Vd, bool sz, Imm<8> imm8) {
    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm8.ZeroExtend() << 2;
    const auto d = ToExtReg(sz, Vd, D);
    const auto base = n == Reg::PC ? ir.Imm32(ir.AlignPC(4)) : ir.GetRegister(n);
    const auto address = U ? ir.Add(base, ir.Imm32(imm32)) : ir.Sub(base, ir.Imm32(imm32));
    if (sz) {
        const auto reg_d = ir.GetExtendedRegister(d);
        auto lo = ir.LeastSignificantWord(reg_d);
        auto hi = ir.MostSignificantWord(reg_d).result;
        if (ir.current_location.EFlag()) {
            std::swap(lo, hi);
        }
        ir.WriteMemory32(address, lo);
        ir.WriteMemory32(ir.Add(address, ir.Imm32(4)), hi);
    } else {
        ir.WriteMemory32(address, ir.GetExtendedRegister(d));
    }

    return true;
}

// VSTM{mode}<c> <Rn>{!}, <list of double registers>
bool ArmTranslatorVisitor::vfp2_VSTM_a1(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm<8> imm8) {
    if (!p && !u && !w) {
        ASSERT_MSG(false, "Decode error");
    }
    
    if (p && !w) {
        ASSERT_MSG(false, "Decode error");
    }

    if (p == u && w) {
        return arm_UDF();
    }

    if (n == Reg::PC && w) {
        return UnpredictableInstruction();
    }

    const auto d = ToExtReg(true, Vd, D);
    const size_t regs = imm8.ZeroExtend() / 2;

    if (regs == 0 || regs > 16 || A32::RegNumber(d)+regs > 32) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm8.ZeroExtend() << 2;
    auto address = u ? ir.GetRegister(n) : IR::U32(ir.Sub(ir.GetRegister(n), ir.Imm32(imm32)));
    if (w) {
        ir.SetRegister(n, u ? IR::U32(ir.Add(address, ir.Imm32(imm32))) : address);
    }
    for (size_t i = 0; i < regs; i++) {
        const auto value = ir.GetExtendedRegister(d + i);
        auto word1 = ir.LeastSignificantWord(value);
        auto word2 = ir.MostSignificantWord(value).result;

        if (ir.current_location.EFlag()) {
            std::swap(word1, word2);
        }

        ir.WriteMemory32(address, word1);
        address = ir.Add(address, ir.Imm32(4));
        ir.WriteMemory32(address, word2);
        address = ir.Add(address, ir.Imm32(4));
    }

    return true;
}

// VSTM{mode}<c> <Rn>{!}, <list of single registers>
bool ArmTranslatorVisitor::vfp2_VSTM_a2(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm<8> imm8) {
    if (!p && !u && !w) {
        ASSERT_MSG(false, "Decode error");
    }

    if (p && !w) {
        ASSERT_MSG(false, "Decode error");
    }

    if (p == u && w) {
        return arm_UDF();
    }

    if (n == Reg::PC && w) {
        return UnpredictableInstruction();
    }

    const auto d = ToExtReg(false, Vd, D);
    const size_t regs = imm8.ZeroExtend();

    if (regs == 0 || A32::RegNumber(d)+regs > 32) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm8.ZeroExtend() << 2;
    auto address = u ? ir.GetRegister(n) : IR::U32(ir.Sub(ir.GetRegister(n), ir.Imm32(imm32)));
    if (w) {
        ir.SetRegister(n, u ? IR::U32(ir.Add(address, ir.Imm32(imm32))) : address);
    }
    for (size_t i = 0; i < regs; i++) {
        const auto word = ir.GetExtendedRegister(d + i);
        ir.WriteMemory32(address, word);
        address = ir.Add(address, ir.Imm32(4));
    }

    return true;
}

// VLDM{mode}<c> <Rn>{!}, <list of double registers>
bool ArmTranslatorVisitor::vfp2_VLDM_a1(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm<8> imm8) {
    if (!p && !u && !w) {
        ASSERT_MSG(false, "Decode error");
    }

    if (p && !w) {
        ASSERT_MSG(false, "Decode error");
    }

    if (p == u && w) {
        return arm_UDF();
    }

    if (n == Reg::PC && w) {
        return UnpredictableInstruction();
    }

    const auto d = ToExtReg(true, Vd, D);
    const size_t regs = imm8.ZeroExtend() / 2;

    if (regs == 0 || regs > 16 || A32::RegNumber(d)+regs > 32) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm8.ZeroExtend() << 2;
    auto address = u ? ir.GetRegister(n) : IR::U32(ir.Sub(ir.GetRegister(n), ir.Imm32(imm32)));
    if (w) {
        ir.SetRegister(n, u ? IR::U32(ir.Add(address, ir.Imm32(imm32))) : address);
    }
    for (size_t i = 0; i < regs; i++) {
        auto word1 = ir.ReadMemory32(address);
        address = ir.Add(address, ir.Imm32(4));
        auto word2 = ir.ReadMemory32(address);
        address = ir.Add(address, ir.Imm32(4));

        if (ir.current_location.EFlag()) {
            std::swap(word1, word2);
        }

        ir.SetExtendedRegister(d + i, ir.Pack2x32To1x64(word1, word2));
    }

    return true;
}

// VLDM{mode}<c> <Rn>{!}, <list of single registers>
bool ArmTranslatorVisitor::vfp2_VLDM_a2(Cond cond, bool p, bool u, bool D, bool w, Reg n, size_t Vd, Imm<8> imm8) {
    if (!p && !u && !w) {
        ASSERT_MSG(false, "Decode error");
    }

    if (p && !w) {
        ASSERT_MSG(false, "Decode error");
    }

    if (p == u && w) {
        return arm_UDF();
    }

    if (n == Reg::PC && w) {
        return UnpredictableInstruction();
    }

    const auto d = ToExtReg(false, Vd, D);
    const size_t regs = imm8.ZeroExtend();

    if (regs == 0 || A32::RegNumber(d)+regs > 32) {
        return UnpredictableInstruction();
    }

    if (!ConditionPassed(cond)) {
        return true;
    }

    const u32 imm32 = imm8.ZeroExtend() << 2;
    auto address = u ? ir.GetRegister(n) : IR::U32(ir.Sub(ir.GetRegister(n), ir.Imm32(imm32)));
    if (w) {
        ir.SetRegister(n, u ? IR::U32(ir.Add(address, ir.Imm32(imm32))) : address);
    }
    for (size_t i = 0; i < regs; i++) {
        const auto word = ir.ReadMemory32(address);
        address = ir.Add(address, ir.Imm32(4));
        ir.SetExtendedRegister(d + i, word);
    }

    return true;
}

} // namespace Dynarmic::A32
