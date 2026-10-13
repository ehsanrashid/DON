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

#if !defined(__INTELLISENSE__)
    #if !defined(__linux__) /* Non-Linux */
        #error "Supported only on Linux"
    #endif

    #include <sys/auxv.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

#include "../utility/types.h"

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

DEFINE_ARCH_ENTRY(riscv64)
DEFINE_ARCH_ENTRY(riscv64_rva23)

namespace {
struct CpuFeatures final {
    bool gcv;        // GCV
    bool zbitmanip;  // Zba + Zbb + Zbs
    bool zicond;     // Zicond
    int  vlen;       // vector length in bits (valid only if gcv is true), always a power of two
};

CpuFeatures query_cpu_features() noexcept {
#if !defined(__INTELLISENSE__)
    const unsigned long     hwcap    = getauxval(AT_HWCAP);
    constexpr unsigned long MASK_GCV = 0x20112DUL;

    bool gcv  = (hwcap & MASK_GCV) == MASK_GCV;
    int  vlen = 0;
    if (gcv)
    {
        unsigned long vlenb = 0;
        asm volatile("csrr %0, 0xc22" : "=r"(vlenb));  // 0xc22 = vlenb
        vlen = int(vlenb * 8);
    }

    constexpr long     NR_RISCV_HWPROBE = 258;
    constexpr DON::i64 KEY_IMA_EXT_0    = 4;
    constexpr DON::u64 EXT_ZB           = DON::u64{1} << 3 | DON::u64{1} << 4 | DON::u64{1} << 5;
    constexpr DON::u64 EXT_ZICOND       = DON::u64{1} << 35;

    struct HwProbePair final {
       public:
        DON::i64 key;
        DON::u64 value;
    };

    HwProbePair probPair{KEY_IMA_EXT_0, 0};

    const long rc        = syscall(NR_RISCV_HWPROBE, &probPair, 1UL, 0UL, nullptr, 0U);
    bool       zbitmanip = false;
    bool       zicond    = false;

    if (rc == 0 && probPair.key == KEY_IMA_EXT_0)
    {
        zbitmanip = (probPair.value & EXT_ZB) == EXT_ZB;
        zicond    = (probPair.value & EXT_ZICOND) == EXT_ZICOND;
    }

    return CpuFeatures{gcv, zbitmanip, zicond, vlen};

#else
    return CpuFeatures{};

#endif
}

// Selects the most capable ISA variant supported by current CPU
int dispatch(const CpuFeatures& cpuFeatures, const int argc, const char* const argv[]) noexcept {
    if (!cpuFeatures.gcv || !cpuFeatures.zbitmanip || !cpuFeatures.zicond || cpuFeatures.vlen < 128)
        return entry_riscv64(argc, argv);

    return entry_riscv64_rva23(argc, argv);
}

}  // namespace

int main(const int argc, const char* const argv[]) noexcept {
    auto cpuFeatures = query_cpu_features();
    return dispatch(cpuFeatures, argc, argv);
}
