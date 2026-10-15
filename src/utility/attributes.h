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

#ifndef UTILITY_ATTRIBUTES_H_INCLUDED
#define UTILITY_ATTRIBUTES_H_INCLUDED

#include "../utility/environment.h"

// Enable GNU-style compiler attributes for Clang and GCC, excluding SWIG.
// Unsupported compilers and SWIG receive an empty attribute macro.
#if defined(CLANG_OR_GCC) && !defined(SWIG)
    #define ATTRIBUTE(x) __attribute__((x))
#else
    #define ATTRIBUTE(x)
#endif

// --------------------

#if defined(CLANG_OR_GCC)
    #define ALWAYS_INLINE inline ATTRIBUTE(always_inline)
#elif defined(_MSC_VER)
    #define ALWAYS_INLINE __forceinline
#else
    #define ALWAYS_INLINE inline
#endif

// clang-format off
#if defined(__clang__)
    #define ASSUME(cond) __builtin_assume(cond)
#elif defined(__GNUC__)
    #if __GNUC__ >= 13
        #define ASSUME(cond) ATTRIBUTE(assume(cond))
    #else
        #define ASSUME(cond) do { if (!(cond)) __builtin_unreachable(); } while (false)
    #endif
#elif defined(_MSC_VER)
    #define ASSUME(cond) __assume(cond)
#else
    #define ASSUME(cond) ((void)0)
#endif
// clang-format on

#if defined(CLANG_OR_GCC)
    #define UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define UNREACHABLE() __assume(false)
#else
    #define UNREACHABLE() ((void) 0)
#endif

#if defined(CLANG_OR_GCC)
    #define RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define RESTRICT __restrict
#else
    #define RESTRICT
#endif

#if defined(CLANG_OR_GCC)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define UNLIKELY(x) (x)
#endif

#define PACKED ATTRIBUTE(packed)

#endif  // UTILITY_ATTRIBUTES_H_INCLUDED
