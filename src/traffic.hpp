// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "live_settings.hpp"
#include <chrono>
#include <mutex>
#include <map>
namespace hls {
class TrafficController {
  public:
    bool acquire(const std::string &ip, unsigned limit, bool upload = false) {
        std::lock_guard<std::mutex> l(mutex_);
        auto &p = peers_[ip];
        if (limit && p.active >= limit)
            return false;
        ++p.active;
        if (upload)
            ++p.uploads;
        else
            ++p.downloads;
        return true;
    }
    void release(const std::string &ip, bool upload = false) {
        std::lock_guard<std::mutex> l(mutex_);
        auto it = peers_.find(ip);
        if (it == peers_.end() || !it->second.active)
            return;
        auto &p = it->second;
        if (upload && p.uploads)
            --p.uploads;
        if (!upload && p.downloads)
            --p.downloads;
        if (!--p.active)
            peers_.erase(it);
    }
    std::size_t preferred_block(const std::string &ip, bool upload, const LiveSettings &settings, std::size_t maximum);
    // Zero grants this block. A positive delay does not reserve future tokens:
    // cancellation therefore cannot leave a debt in another client's schedule.
    std::chrono::steady_clock::duration delay(const std::string &ip, std::size_t bytes, bool upload,
                                              const LiveSettings &settings);

  private:
    using Clock = std::chrono::steady_clock;
    struct Bucket {
        double tokens = 0;
        std::size_t capacity = 0;
        Clock::time_point updated{};
    };
    struct Peer {
        unsigned active = 0, uploads = 0, downloads = 0;
        Bucket up, down;
        Clock::time_point up_demand{}, down_demand{};
    };
    double effective_rate(bool upload, const LiveSettings &settings, Clock::time_point now) const;
    std::mutex mutex_;
    std::map<std::string, Peer> peers_;
    Bucket up_, down_;
    std::uint64_t last_up_ = UINT64_MAX, last_down_ = UINT64_MAX, last_ip_up_ = UINT64_MAX, last_ip_down_ = UINT64_MAX;
};
} // namespace hls
