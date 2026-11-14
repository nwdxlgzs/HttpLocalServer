// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>

namespace hls {
struct ScriptAssetStats {
    std::uint64_t hits = 0, misses = 0, coalesced = 0, extractions = 0, released = 0;
    std::size_t bytes = 0, limit = 0, entries = 0, loading = 0;
};
struct ScriptStats {
    std::uint64_t compilations = 0, calls = 0, created = 0, reused = 0, retired = 0;
    std::size_t bytecode = 0, active = 0, idle = 0;
    ScriptAssetStats assets;
};
}
