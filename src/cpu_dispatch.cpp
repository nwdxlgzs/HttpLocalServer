#include "cpu_kernels.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#elif (defined(__i386__) || defined(__x86_64__)) && !defined(__APPLE__)
#include <cpuid.h>
#endif
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/auxv.h>
#if defined(__riscv)
#include <sys/prctl.h>
#include <unistd.h>
#endif
#endif

namespace hls { namespace magika {
namespace {
struct Capabilities {bool sse2=false,avx=false,avx2=false,avx512=false,neon=false,sve=false,rvv=false;};
#ifdef __APPLE__
bool apple_feature(const char* name,bool fallback=false) {
    int enabled=0;std::size_t bytes=sizeof(enabled);
    return sysctlbyname(name,&enabled,&bytes,nullptr,0)==0 && bytes==sizeof(enabled)?enabled!=0:fallback;
}
#elif defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__x86_64__)
void cpuid(unsigned leaf,unsigned sub,unsigned (&out)[4]) {
#if defined(_MSC_VER)
    int registers[4]{};__cpuidex(registers,static_cast<int>(leaf),static_cast<int>(sub));
    for(int i=0;i<4;++i)out[i]=static_cast<unsigned>(registers[i]);
#else
    __cpuid_count(leaf,sub,out[0],out[1],out[2],out[3]);
#endif
}
std::uint64_t enabled_xstate() {
#ifdef _MSC_VER
    return _xgetbv(0);
#else
    unsigned low=0,high=0;__asm__ volatile("xgetbv":"=a"(low),"=d"(high):"c"(0));
    return (static_cast<std::uint64_t>(high)<<32)|low;
#endif
}
#endif
Capabilities detect() {
    Capabilities result;
#if defined(__APPLE__)
    // Query OS-usable capabilities, including translated Intel processes.
#if HLS_CPU_X86
    result.sse2=apple_feature("hw.optional.sse2");
    result.avx=apple_feature("hw.optional.avx1_0");
    const bool fma=apple_feature("hw.optional.fma");
    result.avx2=result.avx&&fma&&apple_feature("hw.optional.avx2_0");
    result.avx512=result.avx&&fma&&apple_feature("hw.optional.avx512f")&&apple_feature("hw.optional.avx512dq")&&
        apple_feature("hw.optional.avx512cd")&&apple_feature("hw.optional.avx512bw")&&apple_feature("hw.optional.avx512vl");
#elif defined(__aarch64__) || defined(__arm64__)
    result.neon=apple_feature("hw.optional.neon",true); // Advanced SIMD is the arm64 baseline.
#endif
#elif defined(_M_X64) || defined(_M_IX86) || defined(__i386__) || defined(__x86_64__)
    unsigned registers[4]{};cpuid(0,0,registers);const auto highest=registers[0];
    if(highest>=1) {
        cpuid(1,0,registers);result.sse2=(registers[3]&(1u<<26))!=0;
        const bool fma=(registers[2]&(1u<<12))!=0;
        const bool osxsave=(registers[2]&(1u<<27))!=0;
        const bool avx=(registers[2]&(1u<<28))!=0;
        // Never execute XGETBV before CPUID says the OS enables XSAVE.
        const auto xstate=osxsave?enabled_xstate():0;
        result.avx=avx&&(xstate&6)==6;
        if(highest>=7) {
            cpuid(7,0,registers);
            result.avx2=result.avx&&fma&&(registers[1]&(1u<<5));
            const auto avx512_set=(1u<<16)|(1u<<17)|(1u<<28)|(1u<<30)|(1u<<31);
            result.avx512=result.avx&&fma&&(registers[1]&avx512_set)==avx512_set&&(xstate&0xe6)==0xe6;
        }
    }
#elif defined(__aarch64__)
#ifdef __linux__
    const auto hardware=getauxval(AT_HWCAP);result.neon=(hardware&(1ul<<1))!=0;result.sve=(hardware&(1ul<<22))!=0;
#endif
#elif defined(__arm__)
#ifdef __linux__
    result.neon=(getauxval(AT_HWCAP)&(1ul<<12))!=0;
#endif
#elif defined(_M_ARM64) || defined(_M_ARM)
    result.neon=IsProcessorFeaturePresent(19)!=0;
#elif defined(__riscv) && defined(__linux__)
    // HWCAP plus hwprobe establishes ratified V rather than vendor draft 0.7.
    // Some user-mode emulators implement hwprobe but not the per-thread control.
    if(getauxval(AT_HWCAP)&(1ul<<('V'-'A'))) {
        struct Probe {std::int64_t key;std::uint64_t value;} probe{4,0};
        const bool ratified=syscall(258,&probe,1,0,nullptr,0)==0 && probe.key==4 && (probe.value&(1ull<<2));
        const auto control=prctl(70,0,0,0,0); // PR_RISCV_V_GET_CONTROL
        result.rvv=ratified&&(control<0 || (control&3)!=1);
    }
#endif
    return result;
}
std::vector<float> values(std::size_t count,std::uint32_t state) {
    std::vector<float> result(count);
    for(auto& value:result){state=1664525u*state+1013904223u;value=(static_cast<int>((state>>16)&65535)-32768)*0.00003f;}
    return result;
}
bool verify(const Kernel& kernel) {
    for(const auto shape:{std::array<std::size_t,3>{7,17,23},std::array<std::size_t,3>{5,1280,214}}) {
        const auto m=shape[0],k=shape[1],n=shape[2];auto a=values(m*k,1),b=values(k*n,2),bias=values(n,3);
        std::vector<float> expected(m*n),actual(m*n);
        for(bool use_bias:{false,true}) {
            matrix_scalar(a.data(),b.data(),use_bias?bias.data():nullptr,expected.data(),m,k,n);
            kernel.run(a.data(),b.data(),use_bias?bias.data():nullptr,actual.data(),m,k,n);
            for(std::size_t i=0;i<actual.size();++i)if(!std::isfinite(actual[i])||std::abs(actual[i]-expected[i])>0.0001f*(1+std::abs(expected[i])))return false;
        }
    }
    return true;
}
}
std::vector<Kernel> available_kernels() {
    const auto cpu=detect();(void)cpu;std::vector<Kernel> result{{"scalar",matrix_scalar}};
#if HLS_HAVE_SSE2
    if(cpu.sse2)result.push_back({"sse2",matrix_sse2});
#endif
#if HLS_HAVE_AVX
    if(cpu.avx)result.push_back({"avx",matrix_avx});
#endif
#if HLS_HAVE_AVX2
    if(cpu.avx2)result.push_back({"avx2-fma",matrix_avx2});
#endif
#if HLS_HAVE_AVX512
    if(cpu.avx512)result.push_back({"avx512-fma",matrix_avx512});
#endif
#if HLS_HAVE_NEON
    if(cpu.neon)result.push_back({"neon",matrix_neon});
#endif
#if HLS_HAVE_SVE
    if(cpu.sve)result.push_back({"sve",matrix_sve});
#endif
#if HLS_HAVE_RVV
    if(cpu.rvv)result.push_back({"rvv",matrix_rvv});
#endif
    return result;
}
Kernel choose_kernel(const std::string& requested) {
    const auto candidates=available_kernels();
    if(requested!="auto") {
        for(const auto& kernel:candidates)if(kernel.name==requested){if(!verify(kernel))throw std::runtime_error("CPU kernel failed its numerical self-check");return kernel;}
        throw std::runtime_error("CPU kernel is unavailable on this build, CPU or operating system: "+requested);
    }
    auto a=values(8*1280,11),b=values(1280*512,12),bias=values(512,13);std::vector<float> output(8*512);
    auto best=candidates.front();double fastest=std::numeric_limits<double>::infinity();
    volatile float sink=0;
    for(const auto& kernel:candidates) {
        if(!verify(kernel))continue;
        kernel.run(a.data(),b.data(),bias.data(),output.data(),8,1280,512);
        double elapsed=std::numeric_limits<double>::infinity();
        for(int trial=0;trial<3;++trial) {
            const auto start=std::chrono::steady_clock::now();kernel.run(a.data(),b.data(),bias.data(),output.data(),8,1280,512);
            elapsed=std::min(elapsed,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());sink+=output[trial];
        }
        if(elapsed<fastest){fastest=elapsed;best=kernel;}
    }
    (void)sink;return best;
}
} }
