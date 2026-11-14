// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include "script_stats.hpp"
#include "zip_archive.hpp"
#include <array>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>

namespace hls {
class ScriptAssets {
  public:
    explicit ScriptAssets(const Config &config);
    std::shared_ptr<const std::string> get(const std::shared_ptr<ZipArchive> &archive, const ZipEntry &entry);
    void maintain();
    void validate(const std::shared_ptr<ZipArchive> &archive);
    ScriptAssetStats stats();
  private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        std::shared_ptr<const std::string> bytes;
        Clock::time_point used;
        std::size_t charge;
    };
    struct Flight {
        std::condition_variable ready;
        bool done = false;
        std::exception_ptr error;
        std::shared_ptr<const std::string> bytes;
    };
    void decay(Clock::time_point now);
    void bump(const std::string &key);
    double value(const std::string &key, std::size_t bytes) const;
    void erase(std::map<std::string, Entry>::iterator it);
    void prune(Clock::time_point now);
    std::mutex mutex_;
    std::map<std::string, Entry> entries_;
    std::map<std::string, std::shared_ptr<Flight>> loading_;
    std::array<std::uint16_t, 2048> frequency_{};
    Clock::time_point decayed_ = Clock::now();
    unsigned idle_, half_life_, wait_;
    std::size_t loading_bytes_ = 0;
    ScriptAssetStats stats_;
};
}
