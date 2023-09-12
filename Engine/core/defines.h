#pragma once

#include <stdint.h>
#include <float.h>

typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef int8_t S8;
typedef int16_t S16;
typedef int32_t S32;
typedef int64_t S64;

typedef float F32;
typedef double F64;
typedef size_t Size;

#define HE_MIN_U8 0
#define HE_MIN_U16 0
#define HE_MIN_U32 0
#define HE_MIN_U64 0

#define HE_MAX_U8 0xFF
#define HE_MAX_U16 0xFFFF
#define HE_MAX_U32 0xFFFFFFFF
#define HE_MAX_U64 0XFFFFFFFFFFFFFFFF

#define HE_MIN_S8 -127
#define HE_MIN_S16 -32768
#define HE_MIN_S32 -2147483648
#define HE_MIN_S64 -9223372036854775806

#define HE_MAX_S8 128
#define HE_MAX_S16 32769
#define HE_MAX_S32 2147483649
#define HE_MAX_S64 9223372036854775807

#define HE_MIN_F32 FLT_MIN
#define HE_MAX_F32 FLT_MAX

#define HE_MIN_F64 DBL_MIN
#define HE_MAX_F64 DBL_MAX

#define HE_EPSILON_F32 FLT_EPSILON
#define HE_EPSILON_F64 DBL_EPSILON

#define HE_STRINGIFY_(x) #x
#define HE_STRINGIFY(x) HE_STRINGIFY_(x)

#define HE_GLUE_(x, y) x##y
#define HE_GLUE(x, y) HE_GLUE_(x, y)

#define HE_ARRAYCOUNT(array) (sizeof((array)) / sizeof(*(array)))
#define HE_MIN(a, b) ((a) < (b) ? (a) : (b))
#define HE_MAX(a, b) ((a) > (b) ? (a) : (b))
#define HE_CLAMP(value, min, max) HE_MIN(HE_MAX(min, value), max)

// todo(amer): test other compilers, platforms, arhitectures.
// Compiler Macros Wiki: https://sourceforge.net/p/predef/wiki/Home/

#if defined(__clang__)

    #define HE_COMPILER_CLANG 1

#elif defined(__GNUC__)

    #define HE_COMPILER_GCC 1

#elif defined(_MSC_VER)

    #define HE_COMPILER_MSVC 1

#elif

    #error unsupported compiler

#endif

#ifndef HE_COMPILER_CLANG

    #define HE_COMPILER_CLANG 0

#endif

#ifndef HE_COMPILER_MSVC

    #define HE_COMPILER_CLANG 0

#endif

#ifndef HE_COMPILER_GCC

    #define HE_COMPILER_GCC 0

#endif

// Architectures

#if defined(__amd64__) || defined(_M_AMD64)

    #define HE_ARCH_X64 1

#elif defined(__i386__) || defined(_M_IX86)

    #define HE_ARCH_X86 1

#elif defined(__arm__) || defined(_M_ARM)

    #define HE_ARCH_ARM 1

#elif defined(__aarch64__)

    #define HE_ARCH_ARM64 1

#else

    #error unsupported architecture

#endif

#if !defined(HE_ARCH_X64)

    #define HE_ARCH_X64 0

#endif

#if !defined(HE_ARCH_X86)

    #define HE_ARCH_X86 0

#endif

#if !defined(HE_ARCH_ARM)

    #define HE_ARCH_ARM 0

#endif

#if !defined(HE_ARCH_ARM64)

    #define HE_ARCH_ARM64 0

#endif

// Platforms

#if defined(_WIN32) || defined(_WIN64)

    #define HE_OS_WINDOWS 1

#elif defined(__linux__) || defined(__gnu_linux__)

    #define HE_OS_LINUX 1

#elif defined(__APPLE__) && defined(__MACH__)

    #define HE_OS_MAC

#else

    #error unsupported os

#endif

#if !defined(HE_OS_WINDOWS)

    #define HE_OS_WINDOWS 0

#endif

#if !defined(HE_OS_LINUX)

    #define HE_OS_LINUX 0

#endif

#if !defined(HE_OS_MAC)

    #define HE_OS_MAC 0

#endif

#ifdef HE_EXPORT

    #if HE_COMPILER_MSVC

        #define HE_API extern "C" __declspec(dllexport)

    #else

        #define HE_API extern "C" __attribute__((visiblity("default")))

    #endif

#else

    #if HE_COMPILER_MSVC

        #define HE_API extern "C" __declspec(dllimport)

    #else

        #define HE_API extern "C"

    #endif

#endif

#if HE_COMPILER_MSVC

    #define HE_FORCE_INLINE __forceinline

#elif HE_COMPILER_GCC

    #define HE_FORCE_INLINE __attribute__((always_inline))

#elif HE_COMPILER_CLANG

    #define HE_FORCE_INLINE attribute((always_inline))

#error unsupported compiler

#endif

#define HE_DEBUG_BREAK() *((U32*)0) = 0
#define HE_ASSERTIONS 1

#ifdef HE_SHIPPING
#undef HE_ASSERTIONS
#define HE_ASSERTIONS 0
#endif

#if HE_ASSERTIONS

    #define HE_ASSERT(expression)\
    if ((expression))\
    {\
    }\
    else\
    {\
        HE_DEBUG_BREAK();\
    }

#else

    #define HE_ASSERT(expression)

#endif

template< typename F >
struct Defer_Block
{
    F f;

    Defer_Block(F &&f)
        : f(f)
    {
    }

    ~Defer_Block()
    {
        f();
    }
};

#define HE_DEFER Defer_Block HE_GLUE(defer_block_, __COUNTER__) = [&]()

HE_FORCE_INLINE U32 u64_to_u32(U64 value)
{
    HE_ASSERT(value <= HE_MAX_U32);
    return (U32)value;
}

HE_FORCE_INLINE U16 u32_to_u16(U32 value)
{
    HE_ASSERT(value <= HE_MAX_U16);
    return (U16)value;
}

HE_FORCE_INLINE U8 u16_to_u8(U16 value)
{
    HE_ASSERT(value <= HE_MAX_U8);
    return (U8)value;
}

HE_FORCE_INLINE S32 u64_to_s32(U64 value)
{
    HE_ASSERT(value >= 0 && value <= HE_MAX_S32);
    return (S32)value;
}

HE_FORCE_INLINE S16 u32_to_s16(U32 value)
{
    HE_ASSERT(value >= 0 && value <= HE_MAX_S16);
    return (S16)value;
}

HE_FORCE_INLINE S8 u16_to_s8(U16 value)
{
    HE_ASSERT(value >= 0 && value <= HE_MAX_S8);
    return (S8)value;
}