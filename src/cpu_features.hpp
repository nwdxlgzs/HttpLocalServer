// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#pragma once

#ifndef __has_include
#define __has_include(x) 0
#endif
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#define HLS_CPU_X86 1
#else
#define HLS_CPU_X86 0
#endif
#if defined(__GNUC__) || defined(__clang__)
#define HLS_FUNCTION_TARGET(features) __attribute__((target(features),noinline))
#else
#define HLS_FUNCTION_TARGET(features) __declspec(noinline)
#endif

// Optional overrides are disable switches; a normal src/*.cpp build needs none.
#ifndef HLS_HAVE_SSE2
#define HLS_HAVE_SSE2 HLS_CPU_X86
#endif
#ifndef HLS_HAVE_AVX
#define HLS_HAVE_AVX HLS_CPU_X86
#endif
#ifndef HLS_HAVE_AVX2
#define HLS_HAVE_AVX2 HLS_CPU_X86
#endif
#ifndef HLS_HAVE_AVX512
#define HLS_HAVE_AVX512 HLS_CPU_X86
#endif
#ifndef HLS_HAVE_NEON
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64) || defined(_M_ARM) || (defined(__arm__) && __has_include(<arm_neon.h>))
#define HLS_HAVE_NEON 1
#else
#define HLS_HAVE_NEON 0
#endif
#endif
#ifndef HLS_HAVE_SVE
#if !defined(__APPLE__) && defined(__aarch64__) && __has_include(<arm_sve.h>) && ((defined(__clang__) && __clang_major__>=11) || (!defined(__clang__) && __GNUC__>=10))
#define HLS_HAVE_SVE 1
#else
#define HLS_HAVE_SVE 0
#endif
#endif
#ifndef HLS_HAVE_RVV
#if defined(__riscv) && __has_include(<riscv_vector.h>) && ((defined(__clang__) && __clang_major__>=18) || (!defined(__clang__) && __GNUC__>=14))
#define HLS_HAVE_RVV 1
#else
#define HLS_HAVE_RVV 0
#endif
#endif
