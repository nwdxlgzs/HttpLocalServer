#pragma once
#include "config.hpp"
#include "platform.hpp"
#include "worker_pool.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace hls {
struct MemoryAccount {
    std::atomic<std::size_t> allocated{0};
    std::atomic<std::uint64_t> released{0};
    std::atomic<std::size_t> ceiling{0};
};
struct Block {
    Block(std::size_t length, std::shared_ptr<MemoryAccount> account);
    ~Block();
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    char* data = nullptr;
    std::size_t size = 0, capacity = 0;
private:
    std::shared_ptr<MemoryAccount> account_;
};
struct CacheStats {
    std::uint64_t hits = 0, misses = 0, coalesced = 0, disk_bytes = 0, released_bytes = 0;
    std::size_t resident_bytes = 0, allocated_bytes = 0, target_bytes = 0, entries = 0;
};
class HotCache {
public:
    using Callback = std::function<void(std::exception_ptr, std::shared_ptr<Block>)>;
    HotCache(const Config& config, WorkerPool& workers);
    void get(std::shared_ptr<File> file, std::uint64_t block_offset, Callback callback);
    void maintain();
    CacheStats stats();
    std::size_t block_size() const { return config_.block_bytes; }
    std::shared_ptr<Block> transfer_buffer() {return std::make_shared<Block>(config_.block_bytes,memory_);}
private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        std::shared_ptr<Block> block;
        Clock::time_point accessed;
        double heat = 1;
        bool main = false;
        std::list<std::string>::iterator position;
    };
    struct Shard {
        std::mutex mutex;
        std::unordered_map<std::string, Entry> entries;
        std::unordered_map<std::string, std::vector<Callback>> loading;
        std::list<std::string> window, main;
        std::array<std::array<std::uint8_t, 4096>, 4> frequency{};
        Clock::time_point decayed = Clock::now();
        std::size_t window_bytes = 0, main_bytes = 0;
    };
    static constexpr std::size_t shard_count = 16;
    void bump(Shard& shard, std::size_t hash);
    unsigned frequency(const Shard& shard, std::size_t hash) const;
    double value(const Entry& entry, Clock::time_point now) const;
    void erase(Shard& shard, const std::string& key, std::vector<std::shared_ptr<Block>>& retired);
    void admit(Shard& shard, const std::string& key, const std::shared_ptr<Block>& block, std::vector<std::shared_ptr<Block>>& retired);
    void complete(std::size_t shard, const std::string& key, std::exception_ptr error, std::shared_ptr<Block> block);
    Config config_; WorkerPool& workers_;
    std::array<Shard, shard_count> shards_;
    std::shared_ptr<MemoryAccount> memory_;
    std::atomic<std::size_t> target_{0};
    std::atomic<std::uint64_t> hits_{0}, misses_{0}, coalesced_{0}, disk_bytes_{0};
};
}
