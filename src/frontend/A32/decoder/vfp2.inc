// cccc1110________----101-__-0----

// Floating-point three-register data processing instructions
INST(vfp2_VMLA,             "VMLA",                    "cccc11100D00nnnndddd101zN0M0mmmm")
INST(vfp2_VMLS,             "VMLS",                    "cccc11100D00nnnndddd101zN1M0mmmm")
INST(vfp2_VNMLS,            "VNMLS",                   "cccc11100D01nnnndddd101zN0M0mmmm")
INST(vfp2_VNMLA,            "VNMLA",                   "cccc11100D01nnnndddd101zN1M0mmmm")
INST(vfp2_VMUL,             "VMUL",                    "cccc11100D10nnnndddd101zN0M0mmmm")
INST(vfp2_VNMUL,            "VNMUL",                   "cccc11100D10nnnndddd101zN1M0mmmm")
INST(vfp2_VADD,             "VADD",                    "cccc11100D11nnnndddd101zN0M0mmmm")
INST(vfp2_VSUB,             "VSUB",                    "cccc11100D11nnnndddd101zN1M0mmmm")
INST(vfp2_VDIV,             "VDIV",                    "cccc11101D00nnnndddd101zN0M0mmmm")

// Floating-point move instructions
INST(vfp2_VMOV_u32_f64,     "VMOV (core to f64)",      "cccc11100000ddddtttt1011D0010000")
INST(vfp2_VMOV_f64_u32,     "VMOV (f64 to core)",      "cccc11100001nnnntttt1011N0010000")
INST(vfp2_VMOV_u32_f32,     "VMOV (core to f32)",      "cccc11100000nnnntttt1010N0010000")
INST(vfp2_VMOV_f32_u32,     "VMOV (f32 to core)",      "cccc11100001nnnntttt1010N0010000")
INST(vfp2_VMOV_2u32_2f32,   "VMOV (2xcore to 2xf32)",  "cccc11000100uuuutttt101000M1mmmm")
INST(vfp2_VMOV_2f32_2u32,   "VMOV (2xf32 to 2xcore)",  "cccc11000101uuuutttt101000M1mmmm")
INST(vfp2_VMOV_2u32_f64,    "VMOV (2xcore to f64)",    "cccc11000100uuuutttt101100M1mmmm")
INST(vfp2_VMOV_f64_2u32,    "VMOV (f64 to 2xcore)",    "cccc11000101uuuutttt101100M1mmmm")
INST(vfp2_VMOV_reg,         "VMOV (reg)",              "cccc11101D110000dddd101z01M0mmmm")

// Floating-point other instructions
INST(vfp2_VABS,             "VABS",                    "cccc11101D110000dddd101z11M0mmmm")
INST(vfp2_VNEG,             "VNEG",                    "cccc11101D110001dddd101z01M0mmmm")
INST(vfp2_VSQRT,            "VSQRT",                   "cccc11101D110001dddd101z11M0mmmm")
INST(vfp2_VCVT_f_to_f,      "VCVT (f32<->f64)",        "cccc11101D110111dddd101z11M0mmmm")
INST(vfp2_VCVT_to_float,    "VCVT (to float)",         "cccc11101D111000dddd101zs1M0mmmm")
INST(vfp2_VCVT_to_u32,      "VCVT (to u32)",           "cccc11101D111100dddd101zr1M0mmmm")
INST(vfp2_VCVT_to_s32,      "VCVT (to s32)",           "cccc11101D111101dddd101zr1M0mmmm")
INST(vfp2_VCMP,             "VCMP",                    "cccc11101D110100dddd101zE1M0mmmm")
INST(vfp2_VCMP_zero,        "VCMP (with zero)",        "cccc11101D110101dddd101zE1000000")

// Floating-point system register access
INST(vfp2_VMSR,             "VMSR",                    "cccc111011100001tttt101000010000")
INST(vfp2_VMRS,             "VMRS",                    "cccc111011110001tttt101000010000")

// Extension register load-store instructions
INST(vfp2_VPUSH,            "VPUSH",                   "cccc11010D101101dddd101zvvvvvvvv")
INST(vfp2_VPOP,             "VPOP",                    "cccc11001D111101dddd101zvvvvvvvv")
INST(vfp2_VLDR,             "VLDR",                    "cccc1101UD01nnnndddd101zvvvvvvvv")
INST(vfp2_VSTR,             "VSTR",                    "cccc1101UD00nnnndddd101zvvvvvvvv")
INST(vfp2_VSTM_a1,          "VSTM (A1)",               "cccc110puDw0nnnndddd1011vvvvvvvv")
INST(vfp2_VSTM_a2,          "VSTM (A2)",               "cccc110puDw0nnnndddd1010vvvvvvvv")
INST(vfp2_VLDM_a1,          "VLDM (A1)",               "cccc110puDw1nnnndddd1011vvvvvvvv")
INST(vfp2_VLDM_a2,          "VLDM (A2)",               "cccc110puDw1nnnndddd1010vvvvvvvv")
