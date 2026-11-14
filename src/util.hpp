#pragma once
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hls {
std::string json_quote(std::string_view text);
std::string url_encode(std::string_view text);
std::string url_decode(std::string_view text, bool query = false);
bool valid_utf8(std::string_view text);
std::string normalize_path(std::string text);
std::map<std::string, std::string> query_params(std::string_view target);
std::string http_date(std::time_t value);
std::time_t parse_http_date(const std::string& value);
std::string mime_type(std::string name);
std::string lower_ascii(std::string text);
// Only these internal names are listed as frozen placeholders; operations still
// reject every .hls-* path component through normalize_path.
bool frozen_placeholder_name(std::string name);
std::uint64_t parse_unsigned(const std::string& text, std::uint64_t maximum);
enum class RangeState { ignored, valid, unsatisfiable };
struct ByteRange { RangeState state = RangeState::ignored; std::uint64_t first = 0, length = 0; };
ByteRange parse_range(std::string_view text, std::uint64_t size);
bool etag_matches(std::string_view values, std::string_view etag);
}
