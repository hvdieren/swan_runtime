/*
 * Copyright (C) 2010 , Intel Corporation
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef INCLUDED_CILK_COMMON
#define INCLUDED_CILK_COMMON

#ifdef __cplusplus
# include <cassert>
#else
# include <assert.h>
#endif

/* Prefix standard library function and type names with __STDNS in order to
 * get correct lookup in both C and C++.
 */
#ifdef __cplusplus
# define __STDNS std::
#else
# define __STDNS
#endif

/* CILK_EXPORT - Define export of runtime functions from shared library.
 * Should be exported only from cilkrts*.dll/cilkrts*.so
 */
#ifdef _WIN32
# ifdef IN_CILK_RUNTIME
#   define CILK_EXPORT      __declspec(dllexport)
#   define CILK_EXPORT_DATA __declspec(dllexport)
# else
#   define CILK_EXPORT      __declspec(dllimport)
#   define CILK_EXPORT_DATA __declspec(dllimport)
# endif  /* IN_CILK_RUNTIME */
#else /* Unix/gcc */
# ifdef IN_CILK_RUNTIME
#   define CILK_EXPORT      __attribute__((visibility("protected")))
#   define CILK_EXPORT_DATA __attribute__((visibility("protected")))
# else
#   define CILK_EXPORT      /* nothing */
#   define CILK_EXPORT_DATA /* nothing */
# endif  /* IN_CILK_RUNTIME */
#endif /* Unix/gcc */

#ifdef __cplusplus
# define __CILKRTS_BEGIN_EXTERN_C extern "C" {
# define __CILKRTS_END_EXTERN_C }
#else
# define __CILKRTS_BEGIN_EXTERN_C
# define __CILKRTS_END_EXTERN_C
#endif

#ifdef __cplusplus
# ifdef _WIN32
#   define __CILKRTS_NOTHROW __declspec(nothrow)
# else /* Unix/gcc */
#   define __CILKRTS_NOTHROW __attribute__((nothrow))
# endif /* Unix/gcc */
#else
# define __CILKRTS_NOTHROW /* nothing */
#endif /* __cplusplus */

#ifdef _WIN32
# define CILK_ALIGNAS(n) __declspec(align(n))
#else /* Unix/gcc */
# define CILK_ALIGNAS(n) __attribute__((aligned(n)))
#endif /* Unix/gcc */

/* CILK_API: Called explicitly by the programmer.
 * CILK_ABI: Called by compiler-generated code.
 * CILK_ABI_THROWS: An ABI function that may throw an exception
 *
 * Even when these are the same definitions, they should be separate macros so
 * that they can be easily found in the code.
 */

#ifdef _WIN32
# define CILK_API(RET_TYPE) CILK_EXPORT RET_TYPE __CILKRTS_NOTHROW __cdecl
# define CILK_ABI(RET_TYPE) CILK_EXPORT RET_TYPE __CILKRTS_NOTHROW __cdecl
# define CILK_ABI_THROWS(RET_TYPE) CILK_EXPORT RET_TYPE __cdecl
#else
# define CILK_API(RET_TYPE) CILK_EXPORT RET_TYPE __CILKRTS_NOTHROW
# define CILK_ABI(RET_TYPE) CILK_EXPORT RET_TYPE __CILKRTS_NOTHROW
# define CILK_ABI_THROWS(RET_TYPE) CILK_EXPORT RET_TYPE
#endif

/* __CILKRTS_ASSERT should be defined for debugging only, otherwise it
 * interferes with vectorization.  Since NDEBUG is not reliable (it must be
 * set by the user), we must use a platform-specific detection of debug mode.
 */
#if defined(_WIN32) && defined(_DEBUG)
  /* Windows debug */
# define __CILKRTS_ASSERT(e) assert(e)
#elif (! defined(_WIN32)) && ! defined(__OPTIMIZE__)
  /* Unix non-optimized */
# define __CILKRTS_ASSERT(e) assert(e)
#elif defined __cplusplus
  /* C++ non-debug */
# define __CILKRTS_ASSERT(e) static_cast<void>(0)
#else
  /* C non-debug */
# define __CILKRTS_ASSERT(e) ((void) 0)
#endif

// Try to determine if compiler supports rvalue references.
#if defined(__cplusplus) && !defined(__CILKRTS_RVALUE_REFERENCES)
#   if __cplusplus > 201000LL // C++0x
#       define __CILKRTS_RVALUE_REFERENCES 1
#   elif defined(__GXX_EXPERIMENTAL_CXX0X__)
#       define __CILKRTS_RVALUE_REFERENCES 1
#   elif __cplusplus >= 199711LL && __cplusplus < 201000LL
        // __cplusplus has a standard definition prior to 0x
#   elif __INTEL_COMPILER == 1200 && defined(__STDC_HOSTED__)
        // Intel compiler version 12.0
        // __cplusplus has a non-standard definition.  In the absence of a
        // proper definition, look for the C++0x macro, __STDC_HOSTED__.
#       define __CILKRTS_RVALUE_REFERENCES 1
#   elif __INTEL_COMPILER > 1200 && defined(CHAR16T)
        // Intel compiler version 12.1
        // __cplusplus has a non-standard definition.  In the absence of a
        // proper definition, look for the Intel macro, CHAR16T
#       define __CILKRTS_RVALUE_REFERENCES 1
#   endif
#endif

/*
 * Include stdint.h to define the standard integer types.
 *
 * Unfortunately Microsoft doesn't provide stdint.h until Visual Studio 2010,
 * so use our own definitions until those are available
 */

#if ! defined(_MSC_VER) || (_MSC_VER >= 1600)
#include <stdint.h>
#else
#ifndef __MS_STDINT_TYPES_DEFINED__
#define __MS_STDINT_TYPES_DEFINED__
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef __int64 int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned __int64 uint64_t;
#endif  /* __MS_STDINT_TYPES_DEFINED__ */
#endif  /* ! defined(_MSC_VER) || (_MSC_VER >= 1600) */

#endif /* INCLUDED_CILK_COMMON */
