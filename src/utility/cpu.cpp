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

#include "../utility/environment.h"

#if defined(X86)
    #if defined(_MSC_VER)
        #include <emmintrin.h>  // _mm_pause() intrinsic
    #else
    //#include <xmmintrin.h>  // _mm_pause() intrinsic
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(ARM32_V7)
    #if defined(_MSC_VER)
        #include <intrin.h>  // __yield()
    #endif
#elif defined(__wasm__)
    #include <emscripten/threading.h>
#elif defined(_WIN32)
    #include "../utility/platform_win.h"  // YieldProcessor()
#endif

namespace DON {

void cpu_relax() noexcept {
// x86-32 / x86-64
// Covers: Linux, macOS Intel, Windows x86/x64, clang-cl
#if defined(X86)
    #if defined(_MSC_VER)
    ::_mm_pause();  // MSVC intrinsic
    #else
    //_mm_pause();           // Standard SSE intrinsic portable across GCC/Clang/Intel
    __builtin_ia32_pause();  // GCC / Clang intrinsic
    #endif
    // ARM64 / AArch64
    // Covers: Apple Silicon, Windows ARM64, Android, Linux ARM64
#elif defined(__aarch64__) || defined(_M_ARM64)
    #if defined(_MSC_VER)
    __yield();  // MSVC intrinsic
    #elif defined(CLANG_OR_GCC)
    __asm__ __volatile__("yield" ::: "memory");
    #endif
    // ARM32 (ARMv7+)
#elif defined(ARM32_V7)
    #if defined(_MSC_VER)
    __yield();
    #elif defined(CLANG_OR_GCC)
    // Ensure backward-compatible hint space mapping on older ARMv7 cores
    __asm__ __volatile__("yield" ::: "memory");
    #endif
#elif defined(__riscv)
    #if defined(__riscv_zihintpause)
    __builtin_riscv_pause();  // Zihintpause extension
    #else
    // Standard RISC-V fallback: full memory fence; stronger than and not equivalent to PAUSE
    __asm__ __volatile__("fence rw,rw" ::: "memory");
    #endif
#elif defined(__wasm__)
    // Native WebAssembly relax execution block
    __builtin_wasm_memory_atomic_wait32(nullptr, 0, 0);
    // PowerPC / POWER
#elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
    __asm__ __volatile__("or 27,27,27" ::: "memory");  // Low-priority dispatch hint
    // MIPS (ISA revision 2 or later)
#elif defined(__mips__) && defined(__mips_isa_rev) && __mips_isa_rev >= 2
    __asm__ __volatile__("pause" ::: "memory");
    // SPARC
#elif defined(__sparc__)
    __asm__ __volatile__("rd %%ccr, %%g0" ::: "memory");
    // IBM S/390 / z
#elif defined(__s390__) || defined(__s390x__)
    __asm__ __volatile__("bcr 14, 0" ::: "memory");  // Branch-on-condition hint
    // Elbrus 2000 (e2k)
#elif defined(__e2k__)
    __asm__ __volatile__("" ::: "memory");
    // Windows fallback
#elif defined(_WIN32)
    ::YieldProcessor();
    // Compiler barrier when no architecture-specific hint is available
#elif defined(CLANG_OR_GCC)
    __asm__ __volatile__("" ::: "memory");
#else
    // No architecture-specific pause hint available for this compiler
#endif
}

}  // namespace DON
