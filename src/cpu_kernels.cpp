// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
// All optional instructions are isolated by per-function targets.
#include "matrix_kernel.hpp"
#include "cpu_features.hpp"
#if HLS_CPU_X86
#include <immintrin.h>
#endif
#if HLS_HAVE_NEON
#if defined(_MSC_VER) && defined(_M_ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif
#if HLS_HAVE_SVE
#include <arm_sve.h>
#endif
#if HLS_HAVE_RVV
#include <riscv_vector.h>
#endif
#define HLS_MATRIX_BODY \
HLS_TARGET void HLS_MATRIX_NAME(const float* a,const float* b,const float* bias,float* c,size_t rows,size_t inner,size_t columns) { \
    size_t row=0; \
    for(;row+4<=rows;row+=4) { \
        size_t column=0; \
        for(;column+HLS_LANES<=columns;column+=HLS_LANES) { \
            HLS_VECTOR c0=bias?HLS_LOAD(bias+column):HLS_ZERO(),c1=c0,c2=c0,c3=c0; \
            for(size_t k=0;k<inner;++k) { \
                const HLS_VECTOR weights=HLS_LOAD(b+k*columns+column); \
                c0=HLS_MADD(HLS_BROADCAST(a[(row+0)*inner+k]),weights,c0); \
                c1=HLS_MADD(HLS_BROADCAST(a[(row+1)*inner+k]),weights,c1); \
                c2=HLS_MADD(HLS_BROADCAST(a[(row+2)*inner+k]),weights,c2); \
                c3=HLS_MADD(HLS_BROADCAST(a[(row+3)*inner+k]),weights,c3); \
            } \
            HLS_STORE(c+(row+0)*columns+column,c0);HLS_STORE(c+(row+1)*columns+column,c1); \
            HLS_STORE(c+(row+2)*columns+column,c2);HLS_STORE(c+(row+3)*columns+column,c3); \
        } \
        for(;column<columns;++column)for(size_t r=0;r<4;++r) { \
            float value=bias?bias[column]:0.0f; \
            for(size_t k=0;k<inner;++k)value+=a[(row+r)*inner+k]*b[k*columns+column]; \
            c[(row+r)*columns+column]=value; \
        } \
    } \
    for(;row<rows;++row) { \
        size_t column=0; \
        for(;column+HLS_LANES<=columns;column+=HLS_LANES) { \
            HLS_VECTOR value=bias?HLS_LOAD(bias+column):HLS_ZERO(); \
            for(size_t k=0;k<inner;++k)value=HLS_MADD(HLS_BROADCAST(a[row*inner+k]),HLS_LOAD(b+k*columns+column),value); \
            HLS_STORE(c+row*columns+column,value); \
        } \
        for(;column<columns;++column) { \
            float value=bias?bias[column]:0.0f; \
            for(size_t k=0;k<inner;++k)value+=a[row*inner+k]*b[k*columns+column]; \
            c[row*columns+column]=value; \
        } \
    } \
}

namespace hls { namespace magika {
// The universal baseline does not require optional SIMD instructions.
void matrix_scalar(const float* a,const float* b,const float* bias,float* c,size_t rows,size_t inner,size_t columns) {
    for(size_t row=0;row<rows;++row) {
        for(size_t column=0;column<columns;++column)c[row*columns+column]=bias?bias[column]:0.0f;
        for(size_t k=0;k<inner;++k) {
            const auto value=a[row*inner+k];
            for(size_t column=0;column<columns;++column)c[row*columns+column]+=value*b[k*columns+column];
        }
    }
}
} }

#if HLS_HAVE_SSE2
#define HLS_TARGET HLS_FUNCTION_TARGET("sse2")
#define HLS_MATRIX_NAME matrix_sse2
#define HLS_VECTOR __m128
#define HLS_LANES 4
#define HLS_LOAD _mm_loadu_ps
#define HLS_STORE _mm_storeu_ps
#define HLS_ZERO _mm_setzero_ps
#define HLS_BROADCAST _mm_set1_ps
#define HLS_MADD(a,b,c) _mm_add_ps(_mm_mul_ps(a,b),c)
namespace hls { namespace magika {
HLS_MATRIX_BODY
} }

#undef HLS_MATRIX_NAME
#undef HLS_VECTOR
#undef HLS_LANES
#undef HLS_LOAD
#undef HLS_STORE
#undef HLS_ZERO
#undef HLS_BROADCAST
#undef HLS_MADD
#undef HLS_TARGET
#endif
#if HLS_HAVE_AVX
#define HLS_TARGET HLS_FUNCTION_TARGET("avx")
#define HLS_MATRIX_NAME matrix_avx
#define HLS_VECTOR __m256
#define HLS_LANES 8
#define HLS_LOAD _mm256_loadu_ps
#define HLS_STORE _mm256_storeu_ps
#define HLS_ZERO _mm256_setzero_ps
#define HLS_BROADCAST _mm256_set1_ps
#define HLS_MADD(a,b,c) _mm256_add_ps(_mm256_mul_ps(a,b),c)
namespace hls { namespace magika {
HLS_MATRIX_BODY
} }

#undef HLS_MATRIX_NAME
#undef HLS_VECTOR
#undef HLS_LANES
#undef HLS_LOAD
#undef HLS_STORE
#undef HLS_ZERO
#undef HLS_BROADCAST
#undef HLS_MADD
#undef HLS_TARGET
#endif
#if HLS_HAVE_AVX2
#define HLS_TARGET HLS_FUNCTION_TARGET("avx2,fma")
#define HLS_MATRIX_NAME matrix_avx2
#define HLS_VECTOR __m256
#define HLS_LANES 8
#define HLS_LOAD _mm256_loadu_ps
#define HLS_STORE _mm256_storeu_ps
#define HLS_ZERO _mm256_setzero_ps
#define HLS_BROADCAST _mm256_set1_ps
#define HLS_MADD(a,b,c) _mm256_fmadd_ps(a,b,c)
namespace hls { namespace magika {
HLS_MATRIX_BODY
} }

#undef HLS_MATRIX_NAME
#undef HLS_VECTOR
#undef HLS_LANES
#undef HLS_LOAD
#undef HLS_STORE
#undef HLS_ZERO
#undef HLS_BROADCAST
#undef HLS_MADD
#undef HLS_TARGET
#endif
#if HLS_HAVE_AVX512
#define HLS_TARGET HLS_FUNCTION_TARGET("avx512f,avx512dq,avx512cd,avx512bw,avx512vl,fma")
#define HLS_MATRIX_NAME matrix_avx512
#define HLS_VECTOR __m512
#define HLS_LANES 16
#define HLS_LOAD _mm512_loadu_ps
#define HLS_STORE _mm512_storeu_ps
#define HLS_ZERO _mm512_setzero_ps
#define HLS_BROADCAST _mm512_set1_ps
#define HLS_MADD(a,b,c) _mm512_fmadd_ps(a,b,c)
namespace hls { namespace magika {
HLS_MATRIX_BODY
} }

#undef HLS_MATRIX_NAME
#undef HLS_VECTOR
#undef HLS_LANES
#undef HLS_LOAD
#undef HLS_STORE
#undef HLS_ZERO
#undef HLS_BROADCAST
#undef HLS_MADD
#undef HLS_TARGET
#endif
#if HLS_HAVE_NEON
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define HLS_TARGET __attribute__((noinline))
#elif defined(__aarch64__)
#define HLS_TARGET HLS_FUNCTION_TARGET("+simd")
#elif defined(__clang__)
#define HLS_TARGET HLS_FUNCTION_TARGET("neon")
#else
#define HLS_TARGET HLS_FUNCTION_TARGET("fpu=neon")
#endif
#define HLS_MATRIX_NAME matrix_neon
#define HLS_VECTOR float32x4_t
#define HLS_LANES 4
#define HLS_LOAD vld1q_f32
#define HLS_STORE vst1q_f32
#define HLS_ZERO() vdupq_n_f32(0.0f)
#define HLS_BROADCAST vdupq_n_f32
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#define HLS_MADD(a,b,c) vfmaq_f32(c,a,b)
#else
#define HLS_MADD(a,b,c) vmlaq_f32(c,a,b)
#endif
namespace hls { namespace magika {
HLS_MATRIX_BODY
} }

#undef HLS_MATRIX_NAME
#undef HLS_VECTOR
#undef HLS_LANES
#undef HLS_LOAD
#undef HLS_STORE
#undef HLS_ZERO
#undef HLS_BROADCAST
#undef HLS_MADD
#undef HLS_TARGET
#endif
#if HLS_HAVE_SVE
namespace hls { namespace magika {
HLS_FUNCTION_TARGET("+sve") void matrix_sve(const float* a,const float* b,const float* bias,float* c,size_t rows,size_t inner,size_t columns) {
    for(size_t row=0;row<rows;row+=4)for(size_t column=0;column<columns;column+=svcntw()) {
        const svbool_t mask=svwhilelt_b32(static_cast<uint64_t>(column),static_cast<uint64_t>(columns));
        svfloat32_t c0=bias?svld1(mask,bias+column):svdup_f32(0),c1=c0,c2=c0,c3=c0;
        for(size_t k=0;k<inner;++k) {
            const auto weights=svld1(mask,b+k*columns+column);
            c0=svmla_n_f32_m(mask,c0,weights,a[row*inner+k]);
            if(row+1<rows)c1=svmla_n_f32_m(mask,c1,weights,a[(row+1)*inner+k]);
            if(row+2<rows)c2=svmla_n_f32_m(mask,c2,weights,a[(row+2)*inner+k]);
            if(row+3<rows)c3=svmla_n_f32_m(mask,c3,weights,a[(row+3)*inner+k]);
        }
        svst1(mask,c+row*columns+column,c0);
        if(row+1<rows)svst1(mask,c+(row+1)*columns+column,c1);
        if(row+2<rows)svst1(mask,c+(row+2)*columns+column,c2);
        if(row+3<rows)svst1(mask,c+(row+3)*columns+column,c3);
    }
}
} }

#endif
#if HLS_HAVE_RVV
namespace hls { namespace magika {
HLS_FUNCTION_TARGET("arch=+v") void matrix_rvv(const float* a,const float* b,const float* bias,float* c,size_t rows,size_t inner,size_t columns) {
    for(size_t row=0;row<rows;row+=4)for(size_t column=0;column<columns;) {
        const auto length=__riscv_vsetvl_e32m1(columns-column);
        vfloat32m1_t c0=bias?__riscv_vle32_v_f32m1(bias+column,length):__riscv_vfmv_v_f_f32m1(0,length),c1=c0,c2=c0,c3=c0;
        for(size_t k=0;k<inner;++k) {
            const auto weights=__riscv_vle32_v_f32m1(b+k*columns+column,length);
            c0=__riscv_vfmacc_vf_f32m1(c0,a[row*inner+k],weights,length);
            if(row+1<rows)c1=__riscv_vfmacc_vf_f32m1(c1,a[(row+1)*inner+k],weights,length);
            if(row+2<rows)c2=__riscv_vfmacc_vf_f32m1(c2,a[(row+2)*inner+k],weights,length);
            if(row+3<rows)c3=__riscv_vfmacc_vf_f32m1(c3,a[(row+3)*inner+k],weights,length);
        }
        __riscv_vse32_v_f32m1(c+row*columns+column,c0,length);
        if(row+1<rows)__riscv_vse32_v_f32m1(c+(row+1)*columns+column,c1,length);
        if(row+2<rows)__riscv_vse32_v_f32m1(c+(row+2)*columns+column,c2,length);
        if(row+3<rows)__riscv_vse32_v_f32m1(c+(row+3)*columns+column,c3,length);
        column+=length;
    }
}
} }

#endif
