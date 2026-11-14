#include "cache.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hls {
Block::Block(std::size_t length, std::shared_ptr<MemoryAccount> account) : size(length), capacity(page_allocation_size(length)), account_(std::move(account)) {
    auto current = account_->allocated.load();
    do {
        const auto ceiling=account_->ceiling.load();
        if (capacity > ceiling || current > ceiling - capacity) throw Failure(503, "Transfer memory budget exhausted; retry later");
    } while (!account_->allocated.compare_exchange_weak(current, current + capacity));
    try { data = static_cast<char*>(allocate_pages(capacity)); }
    catch (...) { account_->allocated.fetch_sub(capacity); throw; }
}
Block::~Block() {
    release_pages(data, capacity); account_->allocated.fetch_sub(capacity); account_->released.fetch_add(capacity);
}
HotCache::HotCache(const Config& config, WorkerPool& workers) : config_(config), workers_(workers), memory_(std::make_shared<MemoryAccount>()) {
    target_.store(std::min<std::size_t>(config.cache_bytes, static_cast<std::size_t>(available_memory()/8)));
    memory_->ceiling.store(target_.load()+config.buffer_bytes);
}
void HotCache::bump(Shard& shard, std::size_t hash) {
    for (std::size_t row = 0; row < 4; ++row) {
        hash ^= hash >> 17; hash *= static_cast<std::size_t>(0xed5ad4bbU + row * 2);
        auto& counter = shard.frequency[row][hash % 4096];
        if (counter < 255) ++counter;
    }
}
unsigned HotCache::frequency(const Shard& shard, std::size_t hash) const {
    unsigned result = 255;
    for (std::size_t row = 0; row < 4; ++row) {
        hash ^= hash >> 17; hash *= static_cast<std::size_t>(0xed5ad4bbU + row * 2);
        result = std::min(result, static_cast<unsigned>(shard.frequency[row][hash % 4096]));
    }
    return result;
}
double HotCache::value(const Entry& entry, Clock::time_point now) const {
    const double age = std::chrono::duration<double>(now-entry.accessed).count();
    return entry.heat * std::exp2(-age/config_.heat_half_life) / static_cast<double>(entry.block->capacity);
}
void HotCache::erase(Shard& shard, const std::string& key, std::vector<std::shared_ptr<Block>>& retired) {
    auto it = shard.entries.find(key); if (it == shard.entries.end()) return;
    auto& entry = it->second;
    if (entry.main) { shard.main_bytes -= entry.block->capacity; shard.main.erase(entry.position); }
    else { shard.window_bytes -= entry.block->capacity; shard.window.erase(entry.position); }
    retired.push_back(std::move(entry.block)); shard.entries.erase(it);
}
void HotCache::admit(Shard& shard, const std::string& key, const std::shared_ptr<Block>& block, std::vector<std::shared_ptr<Block>>& retired) {
    const auto budget = target_.load() / shard_count;
    if (!budget || block->capacity > budget) return;
    const auto window_budget = std::min(budget, std::max(config_.block_bytes, budget/10));
    const auto main_budget = budget - window_budget;
    const auto now = Clock::now();
    shard.window.push_front(key);
    shard.entries.emplace(key, Entry{block, now, 1, false, shard.window.begin()});
    shard.window_bytes += block->capacity;
    while (shard.window_bytes > window_budget && !shard.window.empty()) {
        const auto candidate_key = shard.window.back();
        auto candidate = shard.entries.find(candidate_key);
        const auto bytes = candidate->second.block->capacity;
        if (bytes > main_budget || frequency(shard, std::hash<std::string>{}(candidate_key)) < 2) { erase(shard, candidate_key, retired); continue; }
        std::vector<std::string> victims; std::size_t reclaimed = 0; bool accepted = true;
        for (auto it = shard.main.rbegin(); shard.main_bytes + bytes > main_budget + reclaimed && it != shard.main.rend(); ++it) {
            const auto& victim = shard.entries.at(*it);
            if (value(candidate->second, now) <= value(victim, now)) { accepted = false; break; }
            victims.push_back(*it); reclaimed += victim.block->capacity;
        }
        if (!accepted || shard.main_bytes + bytes > main_budget + reclaimed) { erase(shard, candidate_key, retired); continue; }
        for (const auto& victim : victims) erase(shard, victim, retired);
        shard.window.erase(candidate->second.position); shard.window_bytes -= bytes;
        shard.main.push_front(candidate_key); shard.main_bytes += bytes;
        candidate->second.main = true; candidate->second.position = shard.main.begin();
    }
}
void HotCache::get(std::shared_ptr<File> file, std::uint64_t block_offset, Callback callback) {
    const auto key = file->info().version + ":" + std::to_string(block_offset);
    const auto hash = std::hash<std::string>{}(key); const auto index = hash % shard_count;
    auto& shard = shards_[index]; std::shared_ptr<Block> hit;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        bump(shard, hash);
        auto it = shard.entries.find(key);
        if (it != shard.entries.end()) {
            auto& entry = it->second;
            const auto now = Clock::now();
            entry.heat = value(entry, now) * entry.block->capacity + 1; entry.accessed = now;
            auto& queue = entry.main ? shard.main : shard.window;
            queue.splice(queue.begin(), queue, entry.position); hit = entry.block; hits_.fetch_add(1);
        } else {
            misses_.fetch_add(1);
            auto pending = shard.loading.find(key);
            if (pending != shard.loading.end()) { pending->second.push_back(std::move(callback)); coalesced_.fetch_add(1); return; }
            shard.loading.emplace(key, std::vector<Callback>{std::move(callback)});
        }
    }
    if (hit) { callback(nullptr, std::move(hit)); return; }
    if (!workers_.submit([this, index, key, file = std::move(file), block_offset] {
        try {
            if (block_offset >= file->info().size) throw Failure(416, "Block offset exceeds file size");
            const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(config_.block_bytes, file->info().size-block_offset));
            auto block = std::make_shared<Block>(bytes, memory_);
            file->read(block_offset, block->data, bytes); disk_bytes_.fetch_add(bytes);
            complete(index, key, nullptr, std::move(block));
        } catch (...) { complete(index, key, std::current_exception(), nullptr); }
    })) complete(index, key, std::make_exception_ptr(Failure(503, "Storage queue is full")), nullptr);
}
void HotCache::complete(std::size_t index, const std::string& key, std::exception_ptr error, std::shared_ptr<Block> block) {
    auto& shard = shards_[index]; std::vector<Callback> callbacks; std::vector<std::shared_ptr<Block>> retired;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto pending = shard.loading.find(key); if (pending == shard.loading.end()) return;
        callbacks = std::move(pending->second); shard.loading.erase(pending);
        if (!error) {
            try { admit(shard, key, block, retired); } catch (...) { /* Cache admission is optional; the read remains usable. */ }
        }
    }
    for (auto& callback : callbacks) { try { callback(error, block); } catch (...) {} }
}
void HotCache::maintain() {
    const auto available = available_memory();
    const auto desired = available < (64ull<<20) ? 0 : std::min<std::uint64_t>(config_.cache_bytes, available/8);
    auto target = target_.load();
    if (desired < target) target = static_cast<std::size_t>(desired);
    else target = std::min<std::size_t>(static_cast<std::size_t>(desired), target + (1ull<<20));
    target_.store(target);
    memory_->ceiling.store(target+config_.buffer_bytes);
    const auto now = Clock::now();
    for (auto& shard : shards_) {
        std::vector<std::shared_ptr<Block>> retired;
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now-shard.decayed).count();
            const auto periods = elapsed / config_.heat_half_life;
            if (periods > 0) {
                const auto shift = static_cast<unsigned>(std::min<std::int64_t>(periods, 8));
                for (auto& row : shard.frequency) for (auto& counter : row) counter >>= shift;
                shard.decayed += std::chrono::seconds(periods*config_.heat_half_life);
            }
            std::vector<std::string> stale;
            for (const auto& pair : shard.entries)
                if (now-pair.second.accessed >= std::chrono::seconds(config_.cache_idle)) stale.push_back(pair.first);
            for (const auto& key : stale) erase(shard, key, retired);
            const auto budget = target/shard_count;
            while (shard.window_bytes+shard.main_bytes > budget) {
                if (!shard.window.empty()) { const auto key = shard.window.back(); erase(shard,key,retired); }
                else if (!shard.main.empty()) { const auto key = shard.main.back(); erase(shard,key,retired); }
                else break;
            }
        }
    }
}
CacheStats HotCache::stats() {
    CacheStats s; s.hits=hits_.load(); s.misses=misses_.load(); s.coalesced=coalesced_.load(); s.disk_bytes=disk_bytes_.load();
    s.allocated_bytes=memory_->allocated.load(); s.released_bytes=memory_->released.load(); s.target_bytes=target_.load();
    for (auto& shard : shards_) { std::lock_guard<std::mutex> lock(shard.mutex); s.resident_bytes += shard.window_bytes+shard.main_bytes; s.entries += shard.entries.size(); }
    return s;
}
}
