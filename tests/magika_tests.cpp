#include "magika.hpp"
#include "cpu_kernels.hpp"
#include "magika_golden.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
hls::magika::Features fixture(int seed){hls::magika::Features result{};for(std::size_t i=0;i<result.size();++i)result[i]=seed==2?256:static_cast<std::int32_t>((i*37+seed*13+11)%257);return result;}
void compare(const hls::magika::Scores& actual,int seed){float maximum=0;for(std::size_t i=0;i<actual.size();++i){check(std::isfinite(actual[i]),"Finite probability");maximum=std::max(maximum,std::abs(actual[i]-reference_scores[seed][i]));}check(maximum<0.0003f,"CPU inference agrees with official ONNX probabilities");}
}
int main(){try{
    for(const auto& kernel:hls::magika::available_kernels()){
        hls::magika::Engine engine(1,kernel.name);for(int seed=0;seed<3;++seed)compare(engine.infer(fixture(seed)),seed);
        std::cout<<kernel.name<<" matched golden scores\n";
    }
    hls::magika::Engine engine(2,"auto",1);std::vector<std::future<hls::magika::Scores>> running;
    for(int i=0;i<6;++i)running.push_back(std::async(std::launch::async,[&,i]{return engine.infer(fixture(i%3));}));
    for(std::size_t i=0;i<running.size();++i)compare(running[i].get(),static_cast<int>(i%3));
    check(engine.statistics().resident_bytes>0&&engine.statistics().active==0,"Bounded shared weights remain after work");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));engine.maintain();check(engine.statistics().resident_bytes==0,"Idle model pages are returned to the OS");
    compare(engine.infer(fixture(1)),1);check(engine.statistics().inferences==7,"Evicted model reloads correctly");
    bool invalid=false;auto malformed=fixture(0);malformed[0]=257;try{engine.infer(malformed);}catch(const std::invalid_argument&){invalid=true;}check(invalid,"Invalid feature tokens rejected");
    invalid=false;try{hls::magika::Engine unsupported(1,"unsupported-isa");}catch(const std::invalid_argument&){invalid=true;}check(invalid,"Unsupported ISA selection rejected before execution");
    std::cout<<"Magika golden, concurrency, eviction and fallback tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
