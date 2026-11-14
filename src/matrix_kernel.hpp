#pragma once
// Optional ISA objects need no C++ runtime or operating-system headers. This
// permits independent ARM/RV32/RV64 arithmetic validation with freestanding tools.
#include <stddef.h>
#include <stdint.h>
namespace hls { namespace magika {
using MatrixKernel=void(*)(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_scalar(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_sse2(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_avx(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_avx2(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_avx512(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_neon(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_sve(const float*,const float*,const float*,float*,size_t,size_t,size_t);
void matrix_rvv(const float*,const float*,const float*,float*,size_t,size_t,size_t);
} }
