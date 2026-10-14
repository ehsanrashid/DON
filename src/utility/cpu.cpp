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

#include "../utility/cpu.h"

#include "../utility/platform.h"

#if defined(X86)
    #if defined(_MSC_VER)
        #include <emmintrin.h>  // _mm_pause() intrinsic
    #endif
#elif defined(_WIN32)
    #include "../platform_win.h"  // YieldProcessor()
#elif defined(_MSC_VER) \
  && (defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM))
    #include <intrin.h>  // __yield()
#endif

namespace DON {

void cpu_relax() noexcept {
// x86-32 / x86-64
// Covers: Linux, macOS Intel, Windows x86/x64, clang-cl
#if defined(X86)
    #if defined(_MSC_VER)
    _mm_pause();  // MSVC intrinsic
    #else
    __builtin_ia32_pause();  // GCC / Clang intrinsic
    #endif
    // ARM64 / AArch64
    // Covers: Apple Silicon, Windows ARM64, Android, Linux ARM64
#elif defined(__aarch64__) || defined(_M_ARM64)
    #if defined(_MSC_VER)
    __yield();  // MSVC intrinsic
    #elif defined(CLANG_OR_GCC)
    __asm__ __volatile__("yield");
    #endif
    // ARM32 (ARMv7+)
#elif (defined(__arm__) || defined(_M_ARM)) && defined(__ARM_ARCH) && (__ARM_ARCH >= 7)
    #if defined(_MSC_VER)
    __yield();
    #elif defined(CLANG_OR_GCC)
    __asm__ __volatile__("yield");
    #endif
    // RISC-V
#elif defined(__riscv)
    #if defined(__riscv_zihintpause)
    __builtin_riscv_pause();  // Zihintpause extension
    #elif defined(CLANG_OR_GCC)
    __asm__ __volatile__("" ::: "memory");
    #endif
    // PowerPC / POWER
#elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
    __asm__ __volatile__("or 27,27,27");  // Low-priority dispatch hint
    // MIPS (ISA revision 2 or later)
#elif defined(__mips__) && defined(__mips_isa_rev) && (__mips_isa_rev >= 2)
    __asm__ __volatile__("pause");
    // LoongArch
#elif defined(__loongarch__)
    __asm__ __volatile__("dbar 0");  // Data completion barrier / Spin-wait hint
    // SPARC
#elif defined(__sparc__)
    __asm__ __volatile__("rd %%ccr, %%g0" ::: "memory");
    // IBM S/390 / z
#elif defined(__s390__) || defined(__s390x__)
    __asm__ __volatile__("bcr 14, 0");  // Branch-on-condition hint
    // Elbrus 2000 (e2k)
#elif defined(__e2k__)
    __asm__ __volatile__("" ::: "memory");
    // Windows fallback
#elif defined(_WIN32)
    YieldProcessor();
    // Compiler barrier when no architecture-specific hint is available
#elif defined(CLANG_OR_GCC)
    __asm__ __volatile__("" ::: "memory");
#else
    // No architecture-specific pause hint available for this compiler
#endif
}

}  // namespace DON
