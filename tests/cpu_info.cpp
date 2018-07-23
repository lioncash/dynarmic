#include <tuple>
#include <vector>

#include <catch.hpp>
#include <fmt/format.h>
#include <xbyak_util.h>

TEST_CASE("Host CPU supports", "[a64]") {
    Xbyak::util::Cpu cpu_info;
    static const std::vector<std::tuple<Xbyak::util::Cpu::Type, const char*>> types {
        std::make_tuple(Xbyak::util::Cpu::tMMX, "MMX"),
        std::make_tuple(Xbyak::util::Cpu::tMMX2, "MMX2"),
        std::make_tuple(Xbyak::util::Cpu::tCMOV, "CMOV"),
        std::make_tuple(Xbyak::util::Cpu::tSSE, "SSE"),
        std::make_tuple(Xbyak::util::Cpu::tSSE2, "SSE2"),
        std::make_tuple(Xbyak::util::Cpu::tSSE3, "SSE3"),
        std::make_tuple(Xbyak::util::Cpu::tSSSE3, "SSSE3"),
        std::make_tuple(Xbyak::util::Cpu::tSSE41, "SSE41"),
        std::make_tuple(Xbyak::util::Cpu::tSSE42, "SSE42"),
        std::make_tuple(Xbyak::util::Cpu::tPOPCNT, "POPCNT"),
        std::make_tuple(Xbyak::util::Cpu::tAESNI, "AESNI"),
        std::make_tuple(Xbyak::util::Cpu::tSSE5, "SSE5"),
        std::make_tuple(Xbyak::util::Cpu::tOSXSAVE, "OSXSAVE"),
        std::make_tuple(Xbyak::util::Cpu::tPCLMULQDQ, "PCLMULQDQ"),
        std::make_tuple(Xbyak::util::Cpu::tAVX, "AVX"),
        std::make_tuple(Xbyak::util::Cpu::tFMA, "FMA"),
        std::make_tuple(Xbyak::util::Cpu::t3DN, "3DN"),
        std::make_tuple(Xbyak::util::Cpu::tE3DN, "E3DN"),
        std::make_tuple(Xbyak::util::Cpu::tSSE4a, "SSE4a"),
        std::make_tuple(Xbyak::util::Cpu::tRDTSCP, "RDTSCP"),
        std::make_tuple(Xbyak::util::Cpu::tAVX2, "AVX2"),
        std::make_tuple(Xbyak::util::Cpu::tBMI1, "BMI1"),
        std::make_tuple(Xbyak::util::Cpu::tBMI2, "BMI2"),
        std::make_tuple(Xbyak::util::Cpu::tLZCNT, "LZCNT"),
        std::make_tuple(Xbyak::util::Cpu::tINTEL, "INTEL"),
        std::make_tuple(Xbyak::util::Cpu::tAMD, "AMD"),
        std::make_tuple(Xbyak::util::Cpu::tENHANCED_REP, "ENHANCED_REP"),
        std::make_tuple(Xbyak::util::Cpu::tRDRAND, "RDRAND"),
        std::make_tuple(Xbyak::util::Cpu::tADX, "ADX"),
        std::make_tuple(Xbyak::util::Cpu::tRDSEED, "RDSEED"),
        std::make_tuple(Xbyak::util::Cpu::tSMAP, "SMAP"),
        std::make_tuple(Xbyak::util::Cpu::tHLE, "HLE"),
        std::make_tuple(Xbyak::util::Cpu::tRTM, "RTM"),
        std::make_tuple(Xbyak::util::Cpu::tF16C, "F16C"),
        std::make_tuple(Xbyak::util::Cpu::tMOVBE, "MOVBE"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512F, "AVX512F"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512DQ, "AVX512DQ"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_IFMA, "AVX512_IFMA"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512IFMA, "AVX512IFMA"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512PF, "AVX512PF"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512ER, "AVX512ER"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512CD, "AVX512CD"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512BW, "AVX512BW"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512VL, "AVX512VL"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_VBMI, "AVX512_VBMI"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512VBMI, "AVX512VBMI"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_4VNNIW, "AVX512_4VNNIW"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_4FMAPS, "AVX512_4FMAPS"),
        std::make_tuple(Xbyak::util::Cpu::tPREFETCHWT1, "PREFETCHWT1"),
        std::make_tuple(Xbyak::util::Cpu::tPREFETCHW, "PREFETCHW"),
        std::make_tuple(Xbyak::util::Cpu::tSHA, "SHA"),
        std::make_tuple(Xbyak::util::Cpu::tMPX, "MPX"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_VBMI2, "AVX512_VBMI2"),
        std::make_tuple(Xbyak::util::Cpu::tGFNI, "GFNI"),
        std::make_tuple(Xbyak::util::Cpu::tVAES, "VAES"),
        std::make_tuple(Xbyak::util::Cpu::tVPCLMULQDQ, "VPCLMULQDQ"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_VNNI, "AVX512_VNNI"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_BITALG, "AVX512_BITALG"),
        std::make_tuple(Xbyak::util::Cpu::tAVX512_VPOPCNTDQ, "AVX512_VPOPCNTDQ"),
    };

    for (const auto& [type, name] : types) {
        if (cpu_info.has(type)) {
            fmt::print("CPU has {}\n", name);
        }
    }
}