// Dedicated C++ evaluator for Google's pinned Apache-2.0 Magika standard_v3_3.
// Model weights/metadata and feature conventions: Copyright Google LLC.
// This evaluates the fixed graph, not arbitrary uploaded ONNX programs.
#include "magika.hpp"
#include "magika_layout.hpp"
#include "cpu_kernels.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace hls { namespace magika {
namespace {
class Floats {
public:
    explicit Floats(std::size_t count) {
#ifdef _WIN32
        SYSTEM_INFO system{};GetSystemInfo(&system);const auto page=static_cast<std::size_t>(system.dwPageSize);
        bytes_=(count*sizeof(float)+page-1)/page*page;
        values_=static_cast<float*>(VirtualAlloc(nullptr,bytes_,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!values_)throw std::bad_alloc();
#else
        const auto page=static_cast<std::size_t>(sysconf(_SC_PAGESIZE));bytes_=(count*sizeof(float)+page-1)/page*page;
        auto* address=mmap(nullptr,bytes_,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(address==MAP_FAILED)throw std::bad_alloc();
        values_=static_cast<float*>(address);
#endif
        std::uninitialized_default_construct_n(values_,count);
    }
    ~Floats(){
#ifdef _WIN32
        VirtualFree(values_,0,MEM_RELEASE);
#else
        munmap(values_,bytes_);
#endif
    }
    Floats(const Floats&)=delete;Floats& operator=(const Floats&)=delete;
    float* get(){return values_;}const float* get()const{return values_;}
    std::size_t bytes()const{return bytes_;}
private:float* values_=nullptr;std::size_t bytes_=0;
};
float gelu(float x) {
    // Keep the ONNX scalar operation order; only the ISA matrix kernels fuse FMA.
    const float p0=0.044714998453855515f*x,p1=x*p0,p2=x*p1;
    const float p3=0.7978845834732056f*(x+p2);
    return x*(0.5f*(1.0f+std::tanh(p3)));
}
std::uint32_t little32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
struct Model {
    Floats storage{data::float_count+257*64};
    const float* tensor(data::Slice slice)const{return storage.get()+slice.offset;}
    const float* embedding()const{return storage.get()+data::float_count;}
    Model() {
        if(std::memcmp(data::weights,"HLSMAG33",8) || little32(data::weights+8)!=1 || little32(data::weights+12)!=data::float_count)throw std::runtime_error("Embedded Magika bundle is invalid");
        const std::uint32_t endian=1;
        if(*reinterpret_cast<const unsigned char*>(&endian))std::memcpy(storage.get(),data::weights+64,data::float_count*sizeof(float));
        else for(std::size_t i=0;i<data::float_count;++i){const auto bits=little32(data::weights+64+i*4);std::memcpy(storage.get()+i,&bits,4);}
        auto* table=storage.get()+data::float_count;const auto* weights=tensor(data::embedding);const auto* bias=tensor(data::embedding_bias);
        for(std::size_t token=0;token<257;++token)for(std::size_t channel=0;channel<64;++channel)table[token*64+channel]=gelu(weights[token*64+channel]+bias[channel]);
    }
};
Scores evaluate(const Model& model,const Kernel& kernel,const Features& features) {
    constexpr std::size_t positions=512,channels=256,patch_width=5,output_channels=512,output_positions=508;
    constexpr auto normalized_count=positions*channels,patch_count=output_positions*patch_width*channels,convolution_count=output_positions*output_channels;
    Floats scratch(normalized_count+patch_count+convolution_count+1024+class_count);
    auto* normalized=scratch.get();auto* patches=normalized+normalized_count;auto* convolutions=patches+patch_count;auto* pooled=convolutions+convolution_count;auto* final_normalized=pooled+512;auto* logits=final_normalized+512;
    const auto* table=model.embedding();
    for(std::size_t token=0;token<feature_count;++token)std::memcpy(normalized+token*64,table+static_cast<std::size_t>(features[token])*64,64*sizeof(float));
    const auto* scale0=model.tensor(data::norm0_scale);const auto* bias0=model.tensor(data::norm0_bias);
    // The first normalization reduces the 512-position axis, not the 256-channel axis.
    for(std::size_t column=0;column<channels;++column) {
        float sum=0,square=0;
        for(std::size_t row=0;row<positions;++row){const auto x=normalized[row*channels+column];sum+=x;square+=x*x;}
        const float mean=sum*(1.0f/512),variance=std::max(0.0f,square*(1.0f/512)-mean*mean),inverse=1.0f/std::sqrt(variance+1e-6f);
        for(std::size_t row=0;row<positions;++row)normalized[row*channels+column]=(normalized[row*channels+column]-mean)*(inverse*scale0[row])+bias0[row];
    }
    for(std::size_t row=0;row<output_positions;++row)std::memcpy(patches+row*patch_width*channels,normalized+row*channels,patch_width*channels*sizeof(float));
    kernel.run(patches,model.tensor(data::convolution),nullptr,convolutions,output_positions,patch_width*channels,output_channels);
    const auto* conv_bias=model.tensor(data::convolution_bias);
    for(std::size_t column=0;column<output_channels;++column) {
        float maximum=-std::numeric_limits<float>::infinity();
        for(std::size_t row=0;row<output_positions;++row)maximum=std::max(maximum,gelu(convolutions[row*output_channels+column]+conv_bias[column]));
        pooled[column]=maximum;
    }
    float sum=0,square=0;
    for(std::size_t i=0;i<512;++i){sum+=pooled[i];square+=pooled[i]*pooled[i];}
    const float mean=sum*(1.0f/512),inverse=1.0f/std::sqrt(std::max(0.0f,square*(1.0f/512)-mean*mean)+1e-6f);
    const auto* scale1=model.tensor(data::norm1_scale);const auto* bias1=model.tensor(data::norm1_bias);
    for(std::size_t i=0;i<512;++i)final_normalized[i]=(pooled[i]-mean)*(inverse*scale1[i])+bias1[i];
    kernel.run(final_normalized,model.tensor(data::classifier),nullptr,logits,1,512,class_count);
    const auto* final_bias=model.tensor(data::classifier_bias);float maximum=-std::numeric_limits<float>::infinity();
    for(std::size_t i=0;i<class_count;++i){logits[i]+=final_bias[i];maximum=std::max(maximum,logits[i]);}
    Scores result{};float denominator=0;
    for(std::size_t i=0;i<class_count;++i){result[i]=std::exp(logits[i]-maximum);denominator+=result[i];}
    if(!std::isfinite(denominator)||denominator<=0)throw std::runtime_error("Magika produced non-finite scores");
    for(auto& probability:result)probability/=denominator;
    return result;
}
}
struct Engine::Impl {
    unsigned concurrency,idle_seconds;std::string requested;std::vector<Kernel> available;
    mutable std::mutex mutex;std::condition_variable ready;std::once_flag tuning;
    std::shared_ptr<Model> model;Kernel kernel{"pending",matrix_scalar};
    std::size_t active=0;std::uint64_t inferences=0;std::chrono::steady_clock::time_point used=std::chrono::steady_clock::now();
    Impl(unsigned count,std::string name,unsigned idle):concurrency(count),idle_seconds(idle),requested(std::move(name)),available(available_kernels()) {
        if(!count||count>4)throw std::invalid_argument("Magika concurrency must be in 1..4");
        if(requested!="auto"&&std::none_of(available.begin(),available.end(),[&](const Kernel& k){return k.name==requested;}))throw std::invalid_argument("Requested CPU kernel is not supported by this build, CPU or OS: "+requested);
    }
};
Engine::Engine(unsigned concurrency,std::string kernel,unsigned idle_seconds):impl_(std::make_unique<Impl>(concurrency,std::move(kernel),idle_seconds)){}
Engine::~Engine()=default;
Scores Engine::infer(const Features& features) {
    for(auto token:features)if(token<0||token>256)throw std::invalid_argument("Magika feature is outside 0..256");
    auto& state=*impl_;
    std::call_once(state.tuning,[&]{auto kernel=choose_kernel(state.requested);std::lock_guard<std::mutex> lock(state.mutex);state.kernel=kernel;});
    std::shared_ptr<Model> model;
    {std::unique_lock<std::mutex> lock(state.mutex);state.ready.wait(lock,[&]{return state.active<state.concurrency;});if(!state.model)state.model=std::make_shared<Model>();model=state.model;++state.active;state.used=std::chrono::steady_clock::now();}
    try {
        auto scores=evaluate(*model,state.kernel,features);
        {std::lock_guard<std::mutex> lock(state.mutex);--state.active;++state.inferences;state.used=std::chrono::steady_clock::now();}state.ready.notify_one();return scores;
    }catch(...){ {std::lock_guard<std::mutex> lock(state.mutex);--state.active;state.used=std::chrono::steady_clock::now();}state.ready.notify_one();throw; }
}
void Engine::maintain(){auto& state=*impl_;std::lock_guard<std::mutex> lock(state.mutex);if(!state.active&&std::chrono::steady_clock::now()-state.used>=std::chrono::seconds(state.idle_seconds))state.model.reset();}
Statistics Engine::statistics()const{auto& state=*impl_;std::lock_guard<std::mutex> lock(state.mutex);Statistics result;result.kernel=state.kernel.name;for(const auto& kernel:state.available)result.available.emplace_back(kernel.name);result.inferences=state.inferences;result.active=state.active;result.resident_bytes=state.model?state.model->storage.bytes():0;return result;}
} }
