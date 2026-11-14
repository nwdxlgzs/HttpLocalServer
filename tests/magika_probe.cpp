#include "magika.hpp"
#include "magika_labels.hpp"
#include "cpu_kernels.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        std::string kernel="auto";int first=1;
        if(argc>2&&std::string(argv[1])=="--kernel"){kernel=argv[2];first=3;}
        if(argc>first&&std::string(argv[first])=="--list"){for(const auto& entry:hls::magika::available_kernels())std::cout<<entry.name<<'\n';return 0;}
        hls::magika::Engine engine(1,kernel);
        for(int item=first;item<argc;++item) {
            std::ifstream input(argv[item],std::ios::binary);if(!input)throw std::runtime_error("Cannot open feature fixture");
            hls::magika::Features features{};
            for(auto& token:features){unsigned char bytes[4];if(!input.read(reinterpret_cast<char*>(bytes),4))throw std::runtime_error("Truncated feature fixture");token=std::int32_t(std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24));}
            char extra;if(input.get(extra))throw std::runtime_error("Feature fixture is too large");
            const auto start=std::chrono::steady_clock::now();const auto scores=engine.infer(features);
            const auto top=static_cast<std::size_t>(std::max_element(scores.begin(),scores.end())-scores.begin());
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"{\"label\":\""<<hls::magika::labels[top].name<<"\",\"kernel\":\""<<engine.statistics().kernel<<"\",\"milliseconds\":"<<elapsed<<",\"scores\":[";
            for(std::size_t i=0;i<scores.size();++i){if(i)std::cout<<',';std::cout<<std::setprecision(9)<<scores[i];}std::cout<<"]}\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
