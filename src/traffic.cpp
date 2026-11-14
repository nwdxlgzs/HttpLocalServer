// SPDX-License-Identifier: Apache-2.0
#include "traffic.hpp"
#include <algorithm>
#include <cmath>
namespace hls {
double TrafficController::effective_rate(bool upload, const LiveSettings &s, Clock::time_point now) const {
    const auto global = upload ? s.upload_kbps : s.download_kbps;
    double rate = static_cast<double>(upload ? s.ip_upload_kbps : s.ip_download_kbps);
    if (global) {
        std::size_t count = 0;
        for (const auto &entry : peers_) {
            const auto demand = upload ? entry.second.up_demand : entry.second.down_demand;
            if (demand != Clock::time_point{} && now - demand < std::chrono::seconds(1))
                ++count;
        }
        const auto fair = static_cast<double>(global) / std::max<std::size_t>(1, count);
        rate = rate ? std::min(rate, fair) : fair;
    }
    return rate;
}
std::size_t TrafficController::preferred_block(const std::string &ip, bool upload, const LiveSettings &s,
                                               std::size_t maximum) {
    if (!(upload ? s.upload_kbps || s.ip_upload_kbps : s.download_kbps || s.ip_download_kbps))
        return maximum;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = Clock::now();
    auto &peer = peers_[ip];
    (upload ? peer.up_demand : peer.down_demand) = now;
    const auto rate = effective_rate(upload, s, now);
    return rate ? static_cast<std::size_t>(
                      std::min<double>(static_cast<double>(maximum), std::max(512.0, rate * 1024.0 / 50.0)))
                : maximum;
}
std::chrono::steady_clock::duration TrafficController::delay(const std::string &ip, std::size_t bytes, bool upload,
                                                             const LiveSettings &s) {
    const auto global = upload ? s.upload_kbps : s.download_kbps;
    std::lock_guard<std::mutex> l(mutex_);
    const auto now = Clock::now();
    if (last_up_ != s.upload_kbps || last_down_ != s.download_kbps || last_ip_up_ != s.ip_upload_kbps ||
        last_ip_down_ != s.ip_download_kbps) {
        last_up_ = s.upload_kbps;
        last_down_ = s.download_kbps;
        last_ip_up_ = s.ip_upload_kbps;
        last_ip_down_ = s.ip_download_kbps;
        up_ = down_ = Bucket{};
        for (auto &p : peers_)
            p.second.up = p.second.down = Bucket{};
    }
    if (!global && !(upload ? s.ip_upload_kbps : s.ip_download_kbps))
        return Clock::duration::zero();
    auto &p = peers_[ip];
    (upload ? p.up_demand : p.down_demand) = now;
    const auto rate = effective_rate(upload, s, now);
    auto &local = upload ? p.up : p.down;
    auto &all = upload ? up_ : down_;
    auto refill = [&](Bucket &b, double kb) {
        if (!kb)
            return;
        b.capacity=std::max<std::size_t>(bytes,static_cast<std::size_t>(std::min(1048576.0,kb*1024.0/10.0)));
        if (b.updated != Clock::time_point{})
            b.tokens =
                std::min<double>(static_cast<double>(b.capacity),
                                 b.tokens + std::chrono::duration<double>(now - b.updated).count() * kb * 1024.0);
        b.updated = now;
    };
    refill(local, rate);
    refill(all, static_cast<double>(global));
    const double local_wait = rate ? std::max(0.0, static_cast<double>(bytes) - local.tokens) / (rate * 1024.0) : 0.0;
    const double global_wait =
        global ? std::max(0.0, static_cast<double>(bytes) - all.tokens) / (static_cast<double>(global) * 1024.0) : 0.0;
    const auto wait = std::max(local_wait, global_wait);
    if (wait > 0)
        return std::chrono::nanoseconds(
            std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(wait * 1000000000.0))));
    if (rate)
        local.tokens -= bytes;
    if (global)
        all.tokens -= bytes;
    return Clock::duration::zero();
}
} // namespace hls
