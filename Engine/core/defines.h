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

#define HOPE_MIN_U8 0
#define HOPE_MIN_U16 0
#define HOPE_MIN_U32 0
#define HOPE_MIN_U64 0

#define HOPE_MAX_U8 0xFF
#define HOPE_MAX_U16 0xFFFF
#define HOPE_MAX_U32 0xFFFFFFFF
#define HOPE_MAX_U64 0XFFFFFFFFFFFFFFFF

#define HOPE_MIN_S8 -127
#define HOPE_MIN_S16 -32768
#define HOPE_MIN_S32 -2147483648
#define HOPE_MIN_S64 -9223372036854775806

#define HOPE_MAX_S8 128
#define HOPE_MAX_S16 32769
#define HOPE_MAX_S32 2147483649
#define HOPE_MAX_S64 9223372036854775807

#define HOPE_MIN_F32 -FLT_MAX
#define HOPE_MAX_F32 FLT_MAX

#define HOPE_MIN_F64 -DBL_MIN
#define HOPE_MAX_F64 DBL_MAX

#define HOPE_EPSILON_F32 FLT_EPSILON
#define HOPE_EPSILON_F64 DBL_EPSILON

#define HOPE_Stringify_(S) #S
#define HOPE_Stringify(S) HOPE_Stringify_(S)

#define HOPE_Glue_(A, B) A##B
#define HOPE_Glue(A, B) HOPE_Glue_(A, B)

#define HOPE_ArrayCount(Array) (sizeof((Array)) / sizeof(*(Array)))
#define HOPE_Min(A, B) ((A) < (B) ? (A) : (B))
#define HOPE_Max(A, B) ((A) > (B) ? (A) : (B))
#define HOPE_Clamp(Value, Min, Max) HOPE_Min(HOPE_Max(Min, Value), Max)

// todo(amer): test other compilers, platforms, arhitectures.
// Compiler Macros Wiki: https://sourceforge.net/p/predef/wiki/Home/

#if defined(__clang__)

    #define HOPE_COMPILER_CLANG 1

#elif defined(__GNUC__)

    #define HOPE_COMPILER_GCC 1

#elif defined(_MSC_VER)

    #define HOPE_COMPILER_MSVC 1

#elif

    #error unsupported compiler

#endif

#ifndef HOPE_COMPILER_CLANG

    #define HOPE_COMPILER_CLANG 0

#endif

#ifndef HOPE_COMPILER_MSVC

    #define HOPE_COMPILER_CLANG 0

#endif

#ifndef HOPE_COMPILER_GCC

    #define HOPE_COMPILER_GCC 0

#endif

// Architectures

#if defined(__amd64__) || defined(_M_AMD64)

    #define HOPE_ARCH_X64 1

#elif defined(__i386__) || defined(_M_IX86)

    #define HOPE_ARCH_X86 1

#elif defined(__arm__) || defined(_M_ARM)

    #define HOPE_ARCH_ARM 1

#elif defined(__aarch64__)

    #define HOPE_ARCH_ARM64 1

#else

    #error unsupported architecture

#endif

#if !defined(HOPE_ARCH_X64)

    #define HOPE_ARCH_X64 0

#endif

#if !defined(HOPE_ARCH_X86)

    #define HOPE_ARCH_X86 0

#endif

#if !defined(HOPE_ARCH_ARM)

    #define HOPE_ARCH_ARM 0

#endif

#if !defined(HOPE_ARCH_ARM64)

    #define HOPE_ARCH_ARM64 0

#endif

// Platforms

#if defined(_WIN32) || defined(_WIN64)

    #define HOPE_OS_WINDOWS 1

#elif defined(__linux__) || defined(__gnu_linux__)

    #define HOPE_OS_LINUX 1

#elif defined(__APPLE__) && defined(__MACH__)

    #define HOPE_OS_MAC

#else

    #error unsupported os

#endif

#if !defined(HOPE_OS_WINDOWS)

    #define HOPE_OS_WINDOWS 0

#endif

#if !defined(HOPE_OS_LINUX)

    #define HOPE_OS_LINUX 0

#endif

#if !defined(HOPE_OS_MAC)

    #define HOPE_OS_MAC 0

#endif

#ifdef HOPE_EXPORT

    #if HOPE_COMPILER_MSVC

        #define HOPE_API __declspec(dllexport)

    #else

        #define HOPE_API __attribute__((visiblity("default")))

    #endif

#else

    #if HOPE_COMPILER_MSVC

        #define HOPE_API __declspec(dllimport)

    #else

        #define HOPE_API

    #endif

#endif

#if HOPE_COMPILER_MSVC

    #define HOPE_FORCE_INLINE __forceinline

#elif HOPE_COMPILER_GCC

    #define HOPE_FORCE_INLINE __attribute__((always_inline))

#elif HOPE_COMPILER_CLANG

    #define HOPE_FORCE_INLINE attribute((always_inline))

#error unsupported compiler

#endif

#define HOPE_DebugBreak() *((U32*)0) = 0
#define HOPE_ASSERTIONS 1

#if HOPE_ASSERTIONS

    #define HOPE_Assert(Expression)\
    if ((Expression))\
    {\
    }\
    else\
    {\
        HOPE_DebugBreak();\
    }

#else

    #define HOPE_Assert(Expression)

#endif

HOPE_FORCE_INLINE U32 u64_to_u32(U64 value)
{
    HOPE_Assert(value <= HOPE_MAX_U32);
    return (U32)value;
}

HOPE_FORCE_INLINE U16 u32_to_u16(U32 value)
{
    HOPE_Assert(value <= HOPE_MAX_U16);
    return (U16)value;
}

HOPE_FORCE_INLINE U8 u16_to_u8(U16 value)
{
    HOPE_Assert(value <= HOPE_MAX_U8);
    return (U8)value;
}

HOPE_FORCE_INLINE S32 u64_to_s32(U64 value)
{
    HOPE_Assert(value >= 0 && value <= HOPE_MAX_S32);
    return (S32)value;
}

HOPE_FORCE_INLINE S16 u32_to_s16(U32 value)
{
    HOPE_Assert(value >= 0 && value <= HOPE_MAX_S16);
    return (S16)value;
}

HOPE_FORCE_INLINE S8 u16_to_s8(U16 value)
{
    HOPE_Assert(value >= 0 && value <= HOPE_MAX_S8);
    return (S8)value;
}