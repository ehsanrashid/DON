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

#include <stdint.h>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/auxv.h>
    #if !defined(HWCAP_ASIMDDP)
        #define HWCAP_ASIMDDP (1 << 20)
    #endif
#endif

#define DEFINE_ARCH_ENTRY(x) \
    namespace DON_##x { \
        extern int main(int argc, const char* argv[]) noexcept; \
    } \
    extern "C" void (*__start_##x##_init[])(void); \
    extern "C" void (*__stop_##x##_init[])(void); \
    int entry_##x(int argc, const char* argv[]) noexcept { \
        unsigned count = __stop_##x##_init - __start_##x##_init; \
        for (unsigned i = 0; i < count; ++i) \
            __start_##x##_init[i](); \
        return DON_##x::main(argc, argv); \
    }

DEFINE_ARCH_ENTRY(armv8)
DEFINE_ARCH_ENTRY(armv8_dotprod)

struct CpuFeatures final {
    bool dotprod;
};

static CpuFeatures query_cpu_features() noexcept {
#if defined(_WIN32)
    return {.dotprod = (bool) IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE)};
#else
    unsigned long hwcap = getauxval(AT_HWCAP);
    return {.dotprod = (bool) (hwcap & HWCAP_ASIMDDP)};
#endif
}

// Selects the most capable ISA variant supported by current CPU
static int dispatch(const CpuFeatures& f, int argc, const char* argv[]) noexcept {
    if (!f.dotprod)
        return entry_armv8(argc, argv);

    return entry_armv8_dotprod(argc, argv);
}

int main(int argc, const char* argv[]) noexcept {
    CpuFeatures features = query_cpu_features();
    return dispatch(features, argc, argv);
}
