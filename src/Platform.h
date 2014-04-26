#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _PLATFORM_H_INC_
#define _PLATFORM_H_INC_

/// To get started type 'make help'.
///
/// For Linux and OSX configuration is done automatically using Makefile.
///
/// For Windows, part of the configuration is detected automatically, but some
/// switches need to be set manually:
///
/// -DNDEBUG    | Disable debugging mode. Always use this.
/// -DPREFETCH  | Enable use of prefetch asm-instruction.
///             | Don't enable it if want the executable to run on some very old machines.
/// -DBSFQ      | Add runtime support for use of Bitscans asm-instruction.
/// -DABM       | Add runtime support for use of ABM asm-instruction. Works only in 64-bit mode.
///             | For compiling requires hardware with ABM support.
/// -DBM2       | Add runtime support for use of BM2 asm-instruction. Works only in 64-bit mode.
///             | For compiling requires hardware with BM2 support.
/// -DLPAGES    | Add runtime support for large pages.

#ifdef _MSC_VER
// Disable some silly and noisy warning from MSVC compiler
#   pragma warning (disable: 4127) // Conditional expression is constant
#   pragma warning (disable: 4146) // Unary minus operator applied to unsigned type
#   pragma warning (disable: 4267) // 'argument' : conversion from '-' to '-', possible loss of data
#   pragma warning (disable: 4800) // Forcing value to bool 'true' or 'false'
#   pragma warning (disable: 6326) // Constant comparison

// MSVC does not support <inttypes.h>
//#   include <stdint.h>
//typedef         int8_t     i08;
//typedef        uint8_t     u08;
//typedef         int16_t    i16;
//typedef        uint16_t    u16;
//typedef         int32_t    i32;
//typedef        uint32_t    u32;
//typedef         int64_t    i64;
//typedef        uint64_t    u64;

typedef   signed __int8     i08;
typedef unsigned __int8     u08;
typedef   signed __int16    i16;
typedef unsigned __int16    u16;
typedef   signed __int32    i32;
typedef unsigned __int32    u32;
typedef   signed __int64    i64;
typedef unsigned __int64    u64;

#   define  S32(X) (X##i32)
#   define  U32(X) (X##ui32)
#   define  S64(X) (X##i64)
#   define  U64(X) (X##ui64)

#else

#   include <inttypes.h>

typedef         int8_t     i08;
typedef        uint8_t     u08;
typedef         int16_t    i16;
typedef        uint16_t    u16;
typedef         int32_t    i32;
typedef        uint32_t    u32;
typedef         int64_t    i64;
typedef        uint64_t    u64;

#   define S32(X) (X##L)
#   define U32(X) (X##UL)
#   define S64(X) (X##LL)
#   define U64(X) (X##ULL)

#endif

// Windows or MinGW
#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

#   ifdef _WIN64
#       ifndef _64BIT
#           define _64BIT
#       endif
#       ifndef BSFQ
#           define BSFQ
#       endif
#   endif

#endif

#undef INLINE

#ifdef _MSC_VER

#   define INLINE     __forceinline

#elif __GNUC__

#   define INLINE     inline __attribute__((always_inline))

#else

#   define INLINE     inline

#endif

#define CACHE_LINE_SIZE 64

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)

#   define CACHE_ALIGN(x)     __declspec(align(x))
//#   define CACHE_ALIGN(x)     alignas(x)

#else

#   define CACHE_ALIGN(x)     __attribute__((aligned(x)))

#endif

// ---

#undef ASSERT
#undef ASSERT_MSG

#ifdef NDEBUG

#   define ASSERT(condition)          ((void) 0)
#   define ASSERT_MSG(condition, msg) ((void) 0)

#else

//#define TRI_LOGGER

//#define CLEANTLOG
//#define OTLOG
//#define ETLOG
#define FTLOG   ExceptLog

#   ifdef TRI_LOGGER

#       include "TriLogger.h"

#       define ASSERT(condition)                \
    do {                                        \
    if (!(condition)) {                         \
    TRI_LOG_MSG ("ASSERT: \'" #condition "\'"); \
    } } while (false)

#       define ASSERT_MSG(condition, msg)       \
    do {                                        \
    if (!(condition)) {                         \
    TRI_LOG_MSG ("ASSERT: \'" msg "\'");        \
    } } while (false)

#   else
#       include <cassert>

#       define ASSERT(condition)          (void)( (!!(condition)) || (_wassert(_CRT_WIDE(#condition), _CRT_WIDE(__FILE__), __LINE__), 0) )
#       define ASSERT_MSG(condition, msg) (void)( (!!(condition)) || (_wassert(_CRT_WIDE(msg),        _CRT_WIDE(__FILE__), __LINE__), 0) )

#   endif

#endif

#endif // _PLATFORM_H_INC_
