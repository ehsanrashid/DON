/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#if defined(_MSC_VER)
    #include <intrin.h>
    #include <cstring>
#else
    #include <cpuid.h>
#endif

#if defined(_MSC_VER)
namespace {

namespace Bit {
// CPUID 1, ECX
constexpr unsigned FMA     = unsigned{1} << 12;
constexpr unsigned SSE41   = unsigned{1} << 19;
constexpr unsigned POPCNT  = unsigned{1} << 23;
constexpr unsigned OSXSAVE = unsigned{1} << 27;
constexpr unsigned AVX     = unsigned{1} << 28;

// CPUID 7.0, EBX
constexpr unsigned BMI        = unsigned{1} << 3;
constexpr unsigned AVX2       = unsigned{1} << 5;
constexpr unsigned BMI2       = unsigned{1} << 8;
constexpr unsigned AVX512F    = unsigned{1} << 16;
constexpr unsigned AVX512DQ   = unsigned{1} << 17;
constexpr unsigned AVX512IFMA = unsigned{1} << 21;
constexpr unsigned AVX512CD   = unsigned{1} << 28;
constexpr unsigned AVX512BW   = unsigned{1} << 30;
constexpr unsigned AVX512VL   = unsigned{1} << 31;

// CPUID 7.0, ECX
constexpr unsigned AVX512VBMI      = unsigned{1} << 1;
constexpr unsigned AVX512VBMI2     = unsigned{1} << 6;
constexpr unsigned GFNI            = unsigned{1} << 8;
constexpr unsigned VAES            = unsigned{1} << 9;
constexpr unsigned VPCLMULQDQ      = unsigned{1} << 10;
constexpr unsigned AVX512VNNI      = unsigned{1} << 11;
constexpr unsigned AVX512BITALG    = unsigned{1} << 12;
constexpr unsigned AVX512VPOPCNTDQ = unsigned{1} << 14;

// CPUID 7.1, EAX
constexpr unsigned AVXVNNI = unsigned{1} << 4;

}  // namespace Bit

bool cpu_supports(const char* feature) noexcept {
    // Required: CPUID intrinsics write all four registers (EAX, EBX, ECX, EDX).
    // regs[0] — EAX
    // regs[1] — EBX
    // regs[2] — ECX
    // regs[3] — EDX
    int regs[4];

    __cpuid(regs, 1);
    const unsigned ecx1 = unsigned{regs[2]};

    if (std::strcmp(feature, "sse4.1") == 0)
        return (ecx1 & Bit::SSE41) != 0;
    if (std::strcmp(feature, "popcnt") == 0)
        return (ecx1 & Bit::POPCNT) != 0;

    if (std::strcmp(feature, "fma") == 0)
    {
        if ((ecx1 & (Bit::FMA | Bit::AVX | Bit::OSXSAVE)) != (Bit::FMA | Bit::AVX | Bit::OSXSAVE))
            return false;

        return (_xgetbv(0) & 0x6) == 0x6;
    }

    __cpuid(regs, 0);
    const unsigned maxLeaf = unsigned{regs[0]};

    if (maxLeaf < 7)
        return false;

    __cpuidex(regs, 7, 0);
    const unsigned maxSubleaf = unsigned{regs[0]};
    const unsigned ebx7       = unsigned{regs[1]};
    const unsigned ecx7       = unsigned{regs[2]};

    if (std::strcmp(feature, "bmi") == 0)
        return (ebx7 & Bit::BMI) != 0;
    if (std::strcmp(feature, "bmi2") == 0)
        return (ebx7 & Bit::BMI2) != 0;

    if (std::strcmp(feature, "avx2") == 0)
    {
        if ((ebx7 & Bit::AVX2) == 0)
            return false;

        if ((ecx1 & (Bit::AVX | Bit::OSXSAVE)) != (Bit::AVX | Bit::OSXSAVE))
            return false;

        return (_xgetbv(0) & 0x6) == 0x6;
    }

    if (std::strcmp(feature, "avxvnni") == 0)
    {
        if (maxSubleaf < 1)
            return false;

        __cpuidex(regs, 7, 1);

        if ((unsigned{regs[0]} & Bit::AVXVNNI) == 0)
            return false;

        if ((ecx1 & (Bit::AVX | Bit::OSXSAVE)) != (Bit::AVX | Bit::OSXSAVE))
            return false;

        return (_xgetbv(0) & 0x6) == 0x6;
    }

    unsigned mask = 0;
    unsigned reg  = 0;

    const bool avx512 = std::strcmp(feature, "avx512f") == 0       //
                     || std::strcmp(feature, "avx512dq") == 0      //
                     || std::strcmp(feature, "avx512ifma") == 0    //
                     || std::strcmp(feature, "avx512cd") == 0      //
                     || std::strcmp(feature, "avx512bw") == 0      //
                     || std::strcmp(feature, "avx512vl") == 0      //
                     || std::strcmp(feature, "avx512vbmi") == 0    //
                     || std::strcmp(feature, "avx512vbmi2") == 0   //
                     || std::strcmp(feature, "avx512vnni") == 0    //
                     || std::strcmp(feature, "avx512bitalg") == 0  //
                     || std::strcmp(feature, "avx512vpopcntdq") == 0;

    if (std::strcmp(feature, "avx512f") == 0)
    {
        mask = Bit::AVX512F;
        reg  = ebx7;
    }
    else if (std::strcmp(feature, "avx512dq") == 0)
    {
        mask = Bit::AVX512DQ;
        reg  = ebx7;
    }
    else if (std::strcmp(feature, "avx512ifma") == 0)
    {
        mask = Bit::AVX512IFMA;
        reg  = ebx7;
    }
    else if (std::strcmp(feature, "avx512cd") == 0)
    {
        mask = Bit::AVX512CD;
        reg  = ebx7;
    }
    else if (std::strcmp(feature, "avx512bw") == 0)
    {
        mask = Bit::AVX512BW;
        reg  = ebx7;
    }
    else if (std::strcmp(feature, "avx512vl") == 0)
    {
        mask = Bit::AVX512VL;
        reg  = ebx7;
    }
    else if (std::strcmp(feature, "avx512vbmi") == 0)
    {
        mask = Bit::AVX512VBMI;
        reg  = ecx7;
    }
    else if (std::strcmp(feature, "avx512vbmi2") == 0)
    {
        mask = Bit::AVX512VBMI2;
        reg  = ecx7;
    }
    else if (std::strcmp(feature, "gfni") == 0)
    {
        mask = Bit::GFNI;
        reg  = ecx7;
    }
    else if (std::strcmp(feature, "vaes") == 0)
    {
        mask = Bit::VAES;
        reg  = ecx7;
    }
    else if (std::strcmp(feature, "vpclmulqdq") == 0)
    {
        mask = Bit::VPCLMULQDQ;
        reg  = ecx7;
    }
    else if (std::strcmp(feature, "avx512vnni") == 0)
    {
        mask = Bit::AVX512VNNI;
        reg  = ecx7;
    }
    else if (std::strcmp(feature, "avx512bitalg") == 0)
    {
        mask = Bit::AVX512BITALG;
        reg  = ecx7;
    }
    else if (std::strcmp(feature, "avx512vpopcntdq") == 0)
    {
        mask = Bit::AVX512VPOPCNTDQ;
        reg  = ecx7;
    }
    else
    {
        return false;
    }

    if (avx512)
    {
        // AVX-512 requires AVX-512F and OS support for opmask and ZMM state.
        if ((ebx7 & Bit::AVX512F) == 0)
            return false;

        if ((ecx1 & (Bit::AVX | Bit::OSXSAVE)) != (Bit::AVX | Bit::OSXSAVE))
            return false;

        if ((_xgetbv(0) & 0xE6) != 0xE6)
            return false;
    }
    else
    {
        // GFNI, VAES and VPCLMULQDQ use AVX vector state in this dispatch.
        if ((ecx7 & (Bit::GFNI | Bit::VAES | Bit::VPCLMULQDQ)) != 0)
        {
            if ((ecx1 & (Bit::AVX | Bit::OSXSAVE)) != (Bit::AVX | Bit::OSXSAVE))
                return false;

            if ((_xgetbv(0) & 0x6) != 0x6)
                return false;
        }
    }

    return (reg & mask) != 0;
}

bool cpu_is(const char* model) noexcept {
    int regs[4] = {};

    // Get vendor string via CPUID 0
    __cpuid(regs, 0);
    char vendor[13] = {};
    std::memcpy(vendor + 0, &regs[1], 4);
    std::memcpy(vendor + 4, &regs[3], 4);
    std::memcpy(vendor + 8, &regs[2], 4);

    // Check vendor first
    if (std::strcmp(model, "amd") == 0)
        return std::strcmp(vendor, "AuthenticAMD") == 0;
    if (std::strcmp(model, "intel") == 0)
        return std::strcmp(vendor, "GenuineIntel") == 0;

    // For model-specific checks, get family/model/stepping
    if (std::strcmp(vendor, "AuthenticAMD") != 0)
        return false;  // Non-AMD CPUs

    __cpuid(regs, 1);
    const unsigned eax       = unsigned{regs[0]};
    const unsigned family    = (eax >> 8) & 0xF;
    const unsigned model_num = (eax >> 4) & 0xF;

    // Extended family/model for modern CPUs
    const unsigned ext_family  = (eax >> 20) & 0xFF;
    const unsigned ext_model   = (eax >> 16) & 0xF;
    const unsigned full_family = (ext_family != 0) ? (ext_family + family) : family;
    const unsigned full_model  = (ext_family != 0) ? ((ext_model << 4) | model_num) : model_num;

    // Excavator (BD-VER4): family 15h, model 60-7F
    if (std::strcmp(model, "bdver4") == 0)
        return full_family == 0x15 && full_model >= 0x60;

    // Zen (ZNVER1): family 17h, model 00-0F
    if (std::strcmp(model, "znver1") == 0)
        return full_family == 0x17 && full_model <= 0x0F;

    // Zen+ / Zen2 (ZNVER2): family 17h, model 10-2F
    if (std::strcmp(model, "znver2") == 0)
        return full_family == 0x17 && full_model >= 0x10;

    return false;
}

}  // namespace

    #define CPU_INIT() ((void) 0)
    #define CPU_SUPPORTS(feature) cpu_supports(feature)
    #define CPU_IS(model) cpu_is(model)

#else
    #define CPU_INIT() __builtin_cpu_init()
    #define CPU_SUPPORTS(feature) __builtin_cpu_supports(feature)
    #define CPU_IS(model) __builtin_cpu_is(model)

#endif

#include "../utility/types.h"

#if defined(__APPLE__)
    // Locate each arch's initializer pointer array at runtime via getsectiondata().
    // Example name is "_i_sse41_popcnt", baseline build is just "_i_"
    #include <stdio.h>
    #include <mach-o/getsect.h>
    #include <sys/sysctl.h>

extern "C" const struct mach_header_64 _mh_execute_header;

    #define DEFINE_ARCH_ENTRY(x) \
        namespace DON_##x { \
            extern int main(const int argc, const char* const argv[]) noexcept; \
        } \
        int entry_##x(const int argc, const char* const argv[]) noexcept { \
            char        name[17]; \
            const char* full = #x; \
            snprintf(name, sizeof(name), "_i_%s", full[6] ? full + 7 : ""); \
            unsigned long size = 0; \
            auto**        fns  = reinterpret_cast<void (**)()>( \
              getsectiondata(&_mh_execute_header, "__DATA", name, &size)); \
            for (unsigned long i = 0; i < size / sizeof(*fns); ++i) \
                fns[i](); \
            return DON_##x::main(argc, argv); \
        }

#else
    #define DEFINE_ARCH_ENTRY(x) \
        namespace DON_##x { \
            extern int main(const int argc, const char* const argv[]) noexcept; \
        } \
        extern "C" void (*__start_##x##_init[])(void); \
        extern "C" void (*__stop_##x##_init[])(void); \
        int entry_##x(const int argc, const char* const argv[]) noexcept { \
            unsigned count = __stop_##x##_init - __start_##x##_init; \
            for (unsigned i = 0; i < count; ++i) \
                __start_##x##_init[i](); \
            return DON_##x::main(argc, argv); \
        }

#endif

DEFINE_ARCH_ENTRY(x86_64)
DEFINE_ARCH_ENTRY(x86_64_sse41_popcnt)
DEFINE_ARCH_ENTRY(x86_64_avx2)
DEFINE_ARCH_ENTRY(x86_64_bmi2)
DEFINE_ARCH_ENTRY(x86_64_avxvnni)
DEFINE_ARCH_ENTRY(x86_64_avx512)
DEFINE_ARCH_ENTRY(x86_64_vnni512)
DEFINE_ARCH_ENTRY(x86_64_avx512icl)

namespace {
struct CpuFeatures final {
    bool sse41;            // SSE4.1
    bool popcnt;           // POPCNT
    bool avx2;             // AVX2
    bool fma;              // FMA
    bool bmi;              // BMI
    bool bmi2;             // BMI2 (may be slow on AMD Excavator and Zen/Zen+/Zen2)
    bool avxvnni;          // AVX-VNNI (non-512-bit dot-product instructions)
    bool avx512f;          // AVX-512 Foundation
    bool avx512bw;         // AVX-512 Byte and Word
    bool avx512cd;         // AVX-512 Conflict Detection
    bool avx512dq;         // AVX-512 Doubleword and Quadword
    bool avx512vl;         // AVX-512 Vector Length
    bool avx512vnni;       // AVX-512 Vector Neural Network Instructions
    bool avx512ifma;       // AVX-512 Integer Fused Multiply-Add
    bool avx512vbmi;       // AVX-512 Vector Bit Manipulation Instructions
    bool avx512vbmi2;      // AVX-512 Vector Bit Manipulation Instructions 2
    bool avx512vpopcntdq;  // AVX-512 Vector Population Count Doubleword and Quadword
    bool avx512bitalg;     // AVX-512 Bit Algorithms
    bool vpclmulqdq;       // VPCLMULQDQ: Vector Carry-less multiplication
    bool gfni;             // Galois Field instructions
    bool vaes;             // VAES: Vector AES instructions
};

CpuFeatures query_cpu_features() noexcept {
    return {
      (bool) CPU_SUPPORTS("sse4.1"),
      (bool) CPU_SUPPORTS("popcnt"),
      (bool) CPU_SUPPORTS("avx2"),
      (bool) CPU_SUPPORTS("fma"),
      (bool) CPU_SUPPORTS("bmi"),
      (bool) CPU_SUPPORTS("bmi2"),
      (bool) CPU_SUPPORTS("avxvnni"),
      (bool) CPU_SUPPORTS("avx512f"),
      (bool) CPU_SUPPORTS("avx512bw"),
      (bool) CPU_SUPPORTS("avx512cd"),
      (bool) CPU_SUPPORTS("avx512dq"),
      (bool) CPU_SUPPORTS("avx512vl"),
      (bool) CPU_SUPPORTS("avx512vnni"),
      (bool) CPU_SUPPORTS("avx512ifma"),
      (bool) CPU_SUPPORTS("avx512vbmi"),
      (bool) CPU_SUPPORTS("avx512vbmi2"),
      (bool) CPU_SUPPORTS("avx512vpopcntdq"),
      (bool) CPU_SUPPORTS("avx512bitalg"),
      (bool) CPU_SUPPORTS("vpclmulqdq"),
      (bool) CPU_SUPPORTS("gfni"),
      (bool) CPU_SUPPORTS("vaes")  //
    };
}

}  // namespace

namespace {
// AMD Excavator (family 15h) and Zen/Zen+/Zen2 (family 17h) implement pext/pdep via microcode.
bool has_slow_bmi2() noexcept {
    return CPU_IS("amd") && (CPU_IS("bdver4") || CPU_IS("znver1") || CPU_IS("znver2"));
}

// Selects the most capable ISA variant supported by current CPU
int dispatch(const CpuFeatures& cpuFeatures, const int argc, const char* const argv[]) noexcept {
    if (!cpuFeatures.sse41 || !cpuFeatures.popcnt)
        return entry_x86_64(argc, argv);

    if (!cpuFeatures.avx2)
        return entry_x86_64_sse41_popcnt(argc, argv);

    if (!cpuFeatures.bmi2 || has_slow_bmi2())
        return entry_x86_64_avx2(argc, argv);

    if (!cpuFeatures.avx512f || !cpuFeatures.avx512bw || !cpuFeatures.avx512cd
        || !cpuFeatures.avx512dq || !cpuFeatures.avx512vl)
    {
        if (!cpuFeatures.avxvnni)
            return entry_x86_64_bmi2(argc, argv);

        return entry_x86_64_avxvnni(argc, argv);
    }

    if (!cpuFeatures.avx512vnni)
        return entry_x86_64_avx512(argc, argv);

    // AVX512ICL requires the full Icelake-client feature suite
    if (!cpuFeatures.avx512ifma          //
        || !cpuFeatures.avx512vbmi       //
        || !cpuFeatures.avx512vbmi2      //
        || !cpuFeatures.avx512vpopcntdq  //
        || !cpuFeatures.avx512bitalg     //
        || !cpuFeatures.vpclmulqdq       //
        || !cpuFeatures.gfni             //
        || !cpuFeatures.vaes)
        return entry_x86_64_vnni512(argc, argv);

    return entry_x86_64_avx512icl(argc, argv);
}

void maybe_promote_thread_to_avx512() noexcept {
#if defined(__APPLE__)
    // Intel Macs supporting AVX512 don't advertise it in xgetbv and only
    // do so once at least one avx512 instruction has been executed.
    // See https://github.com/apple/darwin-xnu/blob/0a798f6738bc1db01281fc08ae024145e84df927/osfmk/i386/fpu.c#L176

    int        supported = 0;
    DON::usize len       = sizeof(supported);
    if (::sysctlbyname("hw.optional.avx512f", &supported, &len, nullptr, 0) == 0 && supported)
        asm volatile(".byte 0x62, 0xf1, 0x7d, 0x48, 0x6f, 0xc0");  // vmovdqa32 zmm0,zmm0
#endif
}

}  // namespace

int main(const int argc, const char* const argv[]) noexcept {
    maybe_promote_thread_to_avx512();

    CPU_INIT();
    auto cpuFeatures = query_cpu_features();
    return dispatch(cpuFeatures, argc, argv);
}
