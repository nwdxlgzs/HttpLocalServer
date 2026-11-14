#pragma once
// Generated from the pinned Google Magika model by tools/models/export_magika.py.
#include <cstddef>
namespace hls { namespace magika { namespace data {
struct Slice {std::size_t offset,count;};
inline constexpr Slice embedding{0,16448};
inline constexpr Slice embedding_bias{16448,64};
inline constexpr Slice norm0_scale{16512,512};
inline constexpr Slice norm0_bias{17024,512};
inline constexpr Slice convolution{17536,655360};
inline constexpr Slice convolution_bias{672896,512};
inline constexpr Slice norm1_scale{673408,512};
inline constexpr Slice norm1_bias{673920,512};
inline constexpr Slice classifier{674432,109568};
inline constexpr Slice classifier_bias{784000,214};
inline constexpr std::size_t float_count=784224;
inline constexpr std::size_t blob_size=3136960;
extern const unsigned char weights[];
} } }
