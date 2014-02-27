//#pragma once
#ifndef PLATFORM_H_
#define PLATFORM_H_

//#undef POPCNT
//#define POPCNT
//#define BSFQ
//#define PREFETCH
//#define LPAGES
//#define __INTEL_COMPILER

//#pragma comment (linker, "/stack:xxx /heap:yyy")

// Windows or MinGW
#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

#   if defined(_MSC_VER)
// Disable some silly and noisy warning from MSVC compiler
#       pragma warning (disable: 4127) // Conditional expression is constant
#       pragma warning (disable: 4146) // Unary minus operator applied to unsigned type
#       pragma warning (disable: 4267) // 'argument' : conversion from '-' to '-', possible loss of data
#       pragma warning (disable: 4800) // Forcing value to bool 'true' or 'false'
#       pragma warning (disable: 6326) // Constant comparison

#       define  S32(X) (X## i32)
#       define  U32(X) (X##ui32)
#       define  S64(X) (X## i64)
#       define  U64(X) (X##ui64)

#   else

#       define S32(X) (X## L )
#       define U32(X) (X##UL )
#       define S64(X) (X## LL)
#       define U64(X) (X##ULL)

#   endif

#   include <cstdint>

typedef   signed __int8          int8_t;
typedef unsigned __int8         uint8_t;
typedef   signed __int16         int16_t;
typedef unsigned __int16        uint16_t;
typedef   signed __int32         int32_t;
typedef unsigned __int32        uint32_t;
typedef   signed __int64         int64_t;
typedef unsigned __int64        uint64_t;

#   if defined(_WIN64) && !defined(_64BIT)
#       define _64BIT
#   endif

#elif defined(__GNUC__)

#   include <inttypes.h>
#   include <unistd.h>  // Used by sysconf(_SC_NPROCESSORS_ONLN)

#   define S32(X) (X## L )
#   define U32(X) (X##UL )
#   define S64(X) (X## LL)
#   define U64(X) (X##ULL)

#else

#   include <unistd.h>  // Used by sysconf(_SC_NPROCESSORS_ONLN)

typedef   signed char            int8_t;
typedef unsigned char           uint8_t;
typedef   signed short           int16_t;
typedef unsigned short          uint16_t;
typedef   signed int             int32_t;
typedef unsigned int            uint32_t;
typedef   signed long long       int64_t;
typedef unsigned long long      uint64_t;

#   define S32(X) (X## L )
#   define U32(X) (X##UL )
#   define S64(X) (X## LL)
#   define U64(X) (X##ULL)

#endif

#   undef INLINE

#if defined(_MSC_VER)

#   define INLINE     __forceinline

#elif defined(__GNUC__)

#   define INLINE     inline __attribute__((always_inline))

#else

#   define INLINE     inline

#endif

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

#endif
