#pragma once
#include "matrix_kernel.hpp"
#include "cpu_features.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hls { namespace magika {
struct Kernel {const char* name;MatrixKernel run;};
void matrix_scalar(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
std::vector<Kernel> available_kernels();
Kernel choose_kernel(const std::string& requested);
#if HLS_HAVE_SSE2
void matrix_sse2(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
#endif
#if HLS_HAVE_AVX
void matrix_avx(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
#endif
#if HLS_HAVE_AVX2
void matrix_avx2(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
#endif
#if HLS_HAVE_AVX512
void matrix_avx512(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
#endif
#if HLS_HAVE_NEON
void matrix_neon(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
#endif
#if HLS_HAVE_SVE
void matrix_sve(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
#endif
#if HLS_HAVE_RVV
void matrix_rvv(const float*,const float*,const float*,float*,std::size_t,std::size_t,std::size_t);
#endif
} }
