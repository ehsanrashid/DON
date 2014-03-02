#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _PLATFORM_H_INC_
#define _PLATFORM_H_INC_

//#undef POPCNT
//#pragma comment (linker, "/stack:xxx /heap:yyy")



/// For Linux and OSX configuration is done automatically using Makefile. To get
/// started type 'make help'.
///
/// For Windows, part of the configuration is detected automatically, but some
/// switches need to be set manually:
///
/// -DNDEBUG    | Disable debugging mode. Always use this.
///
/// -DPREFETCH  | Enable use of prefetch asm-instruction. Must disable it 
///             | if you want the executable to run on some very old machines.
///
/// -DPOPCNT    | Add runtime support for use of popcnt asm-instruction. Works
///             | only in 64-bit mode. For compiling requires hardware with
///             | popcnt support.
///
/// - DBSFQ     | 
/// - DLPAGES   | Enable Large Pages

#ifdef _MSC_VER
// Disable some silly and noisy warning from MSVC compiler
#   pragma warning (disable: 4127) // Conditional expression is constant
#   pragma warning (disable: 4146) // Unary minus operator applied to unsigned type
#   pragma warning (disable: 4267) // 'argument' : conversion from '-' to '-', possible loss of data
#   pragma warning (disable: 4800) // Forcing value to bool 'true' or 'false'
#   pragma warning (disable: 6326) // Constant comparison

// MSVC does not support <inttypes.h>
//#       include <stdint.h>
typedef   signed __int8          int8_t;
typedef unsigned __int8         uint8_t;
typedef   signed __int16         int16_t;
typedef unsigned __int16        uint16_t;
typedef   signed __int32         int32_t;
typedef unsigned __int32        uint32_t;
typedef   signed __int64         int64_t;
typedef unsigned __int64        uint64_t;

#   define  S32(X) (X## i32)
#   define  U32(X) (X##ui32)
#   define  S64(X) (X## i64)
#   define  U64(X) (X##ui64)

#else

#   include <inttypes.h>

#   define S32(X) (X## L )
#   define U32(X) (X##UL )
#   define S64(X) (X## LL)
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
#define FTLOG   except_log

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
