// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include "script_stats.hpp"
#include <atomic>
#include <memory>
#include <mutex>
namespace hls {
class ConfigStore;
class StateStore;
class IsolatedScript;
class ScriptEngine {
  public:
    explicit ScriptEngine(const Config &config);
    ~ScriptEngine();
    void mutation_mutex(std::mutex *mutex, const ConfigStore *config, const StateStore *state);
    bool asset(const std::string &path, std::string &bytes, std::string &mime);
    bool enabled() const { return !!isolated_ || !!workspace_; }
    std::string invoke(const std::string &hook, const std::string &input);
    bool protects(const std::filesystem::path &candidate) const;
    const std::filesystem::path &path() const { return path_; }
    void maintain();
    ScriptStats stats();

  private:
    struct Workspace;
    std::unique_ptr<Workspace> workspace_;
    std::unique_ptr<IsolatedScript> isolated_;
    std::string identity_;
    std::filesystem::path path_;
    unsigned timeout_;
    Config config_;
};
bool safe_response_header(const std::string &name, const std::string &value);
} // namespace hls
