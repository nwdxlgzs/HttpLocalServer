#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hls { namespace magika {
inline constexpr std::size_t feature_count=2048,class_count=214;
using Features=std::array<std::int32_t,feature_count>;
using Scores=std::array<float,class_count>;
struct Statistics {
    std::string kernel;
    std::vector<std::string> available;
    std::uint64_t inferences=0;
    std::size_t resident_bytes=0,active=0;
};
class Engine {
public:
    explicit Engine(unsigned concurrency=2,std::string kernel="auto",unsigned idle_seconds=60);
    ~Engine();
    Engine(const Engine&)=delete;
    Engine& operator=(const Engine&)=delete;
    Scores infer(const Features& features);
    void maintain();
    Statistics statistics() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} }
