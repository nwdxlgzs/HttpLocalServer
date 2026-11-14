// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include "script_stats.hpp"
#include <memory>

namespace hls {
class IsolatedScript {
  public:
    IsolatedScript(const Config &, const std::string &source, const std::string &name);
    ~IsolatedScript();
    std::string invoke(const std::string &hook, const std::string &input);
    void maintain();
    ScriptStats stats();
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
