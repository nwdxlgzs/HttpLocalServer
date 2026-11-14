// SPDX-License-Identifier: Apache-2.0
#include "script_assets.hpp"
#include <algorithm>
#include <cmath>
#include <functional>

namespace hls {
ScriptAssets::ScriptAssets(const Config &c) : idle_(c.cache_idle), half_life_(c.heat_half_life), wait_(c.script_timeout_ms) {
    stats_.limit = c.workspace_cache_bytes;
}
void ScriptAssets::decay(Clock::time_point now) {
    const auto periods = std::chrono::duration_cast<std::chrono::seconds>(now - decayed_).count() / half_life_;
    if (periods <= 0)
        return;
    const auto shift = static_cast<unsigned>(std::min<std::int64_t>(16, periods));
    for (auto &n : frequency_)
        n = static_cast<std::uint16_t>(n >> shift);
    decayed_ += std::chrono::seconds(periods * half_life_);
}
void ScriptAssets::bump(const std::string &key) {
    const auto hash = std::hash<std::string>{}(key);
    for (auto index : {hash & 1023, std::size_t(1024) + ((hash >> 10) & 1023)})
        if (frequency_[index] < 65535)
            ++frequency_[index];
}
double ScriptAssets::value(const std::string &key, std::size_t bytes) const {
    const auto hash = std::hash<std::string>{}(key);
    const auto count = std::min(frequency_[hash & 1023], frequency_[1024 + ((hash >> 10) & 1023)]);
    return static_cast<double>(count) / std::sqrt(static_cast<double>(bytes));
}
void ScriptAssets::erase(std::map<std::string, Entry>::iterator it) {
    stats_.bytes -= it->second.charge;
    stats_.released += it->second.charge;
    entries_.erase(it);
}
void ScriptAssets::prune(Clock::time_point now) {
    decay(now);
    for (auto it = entries_.begin(); it != entries_.end();)
        if (now - it->second.used >= std::chrono::seconds(idle_))
            erase(it++);
        else
            ++it;
}
void ScriptAssets::validate(const std::shared_ptr<ZipArchive> &archive) {
    if (archive->source()->unchanged())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    while (!entries_.empty())
        erase(entries_.begin());
    throw Failure(409, "Workspace ZIP changed; restart with the updated package");
}
std::shared_ptr<const std::string> ScriptAssets::get(const std::shared_ptr<ZipArchive> &archive, const ZipEntry &entry) {
    validate(archive);
    const auto &key = entry.path;
    std::shared_ptr<Flight> flight;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        prune(now);
        bump(key);
        const auto cached = entries_.find(key);
        if (cached != entries_.end()) {
            cached->second.used = now;
            ++stats_.hits;
            return cached->second.bytes;
        }
        ++stats_.misses;
        const auto loading = loading_.find(key);
        if (loading != loading_.end()) {
            flight = loading->second;
            ++stats_.coalesced;
            if (!flight->ready.wait_for(lock, std::chrono::milliseconds(wait_), [&] { return flight->done; }))
                throw Failure(503, "Workspace asset is loading; retry later");
            if (flight->error)
                std::rethrow_exception(flight->error);
            return flight->bytes;
        }
        // Extraction buffers and pending-key metadata have their own fixed
        // ceiling, including when retained caching is disabled.
        if (entry.size > (2ull << 20))
            throw Failure(413, "Workspace static asset exceeds 2 MiB");
        if (loading_.size() >= 32 || entry.size > (8ull << 20) - loading_bytes_)
            throw Failure(503, "Workspace asset extraction budget reached");
        flight = std::make_shared<Flight>();
        loading_.emplace(key, flight);
        loading_bytes_ += static_cast<std::size_t>(entry.size);
        ++stats_.extractions;
    }
    std::shared_ptr<std::string> result;
    std::exception_ptr error;
    try {
        result = std::make_shared<std::string>();
        result->reserve(static_cast<std::size_t>(entry.size));
        archive->extract(entry, [&](const void *p, std::size_t n) { result->append(static_cast<const char *>(p), n); });
        validate(archive);
    } catch (...) {
        error = std::current_exception();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!error) {
            // Admission is optional. Allocation failure must still complete
            // every waiter and return the already extracted response.
            try {
                const auto charge = result->capacity() + key.capacity() + 256;
                if (charge <= stats_.limit) {
                    bool admit = true;
                    while (!entries_.empty() && (stats_.bytes > stats_.limit - charge || entries_.size() >= 512)) {
                        auto victim = std::min_element(entries_.begin(), entries_.end(), [&](const auto &a, const auto &b) {
                            const auto av = value(a.first, a.second.charge), bv = value(b.first, b.second.charge);
                            return av < bv || (av == bv && a.second.used < b.second.used);
                        });
                        if (value(key, charge) < value(victim->first, victim->second.charge)) {
                            admit = false;
                            break;
                        }
                        erase(victim);
                    }
                    if (admit) {
                        entries_.emplace(key, Entry{result, Clock::now(), charge});
                        stats_.bytes += charge;
                    }
                }
            } catch (const std::bad_alloc &) {
            }
            flight->bytes = result;
        }
        flight->error = error;
        flight->done = true;
        loading_.erase(key);
        loading_bytes_ -= static_cast<std::size_t>(entry.size);
    }
    flight->ready.notify_all();
    if (error)
        std::rethrow_exception(error);
    return result;
}
void ScriptAssets::maintain() {
    std::lock_guard<std::mutex> lock(mutex_);
    prune(Clock::now());
}
ScriptAssetStats ScriptAssets::stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = stats_;
    result.entries = entries_.size();
    result.loading = loading_.size();
    return result;
}
}
