// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include <quickjs.h>
#include <vector>

namespace hls {
// Only compile trusted source loaded by this process. Never accept serialized
// QuickJS objects from files, packages or clients as an executable input.
std::vector<std::uint8_t> compile_script(JSContext *ctx, const char *source, std::size_t length,
                                         const char *name, std::size_t maximum);
JSValue evaluate_script(JSContext *ctx, const std::vector<std::uint8_t> &bytecode);
}
