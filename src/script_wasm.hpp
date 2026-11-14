// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <quickjs.h>
namespace hls {
class WasmBridge {
  public:
    using Asset = std::function<std::string(const std::string &)>;
    WasmBridge(JSContext *, const Config &, Asset = {});
    ~WasmBridge();
    void install(JSValueConst api);
    void deadline(std::chrono::steady_clock::time_point);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hls
