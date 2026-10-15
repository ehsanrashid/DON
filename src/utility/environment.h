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

#ifndef UTILITY_ENVIRONMENT_H_INCLUDED
#define UTILITY_ENVIRONMENT_H_INCLUDED

// Architecture ---------------------------------

#if defined(__i386__) || defined(_M_IX86)
    #define X86
    #define X86_32
#elif defined(__x86_64__) || defined(_M_X64)
    #define X86
    #define X86_64
#endif

#if (defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 7) /**/ \
  || (defined(_M_ARM) && _M_ARM >= 7)
    #define ARM32_V7
#endif

// Compiler  ------------------------------------

#if defined(__clang__) || defined(__GNUC__)
    #define CLANG_OR_GCC
#endif

#if defined(__GNUC__) && !defined(__clang__)
    #define GCC_NON_CLANG
#endif

#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
    #define GCC_NON_INTEL
#endif

#if defined(__GNUC__) && !defined(__INTEL_LLVM_COMPILER)
    #define GCC_NON_INTEL_LLVM
#endif


// Platform -------------------------------------

#if defined(__linux__) && !defined(__ANDROID__)
    #define LINUX_NON_ANDROID
#endif

#endif  // UTILITY_ENVIRONMENT_H_INCLUDED
