// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include "platform.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <quickjs.h>
namespace hls {
// Own this object until before JS_FreeContext. No raw native handles enter JS.
class ScriptComponents {
  public:
    using Open = std::function<std::shared_ptr<File>(const std::string &)>;
    using Asset = std::function<std::string(const std::string &)>;
    ScriptComponents(JSContext *, const Config &, Open = {}, Asset = {});
    ~ScriptComponents();
    void install(JSValueConst api);
    void deadline(std::chrono::steady_clock::time_point);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string js_text(JSContext *, JSValueConst);
std::string js_bytes(JSContext *, JSValueConst, std::size_t maximum = 2u << 20);
JSValue js_json(JSContext *, const std::string &);
} // namespace hls
