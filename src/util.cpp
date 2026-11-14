#include "util.hpp"
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace hls {
std::string json_quote(std::string_view text) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    result += '"'; return result;
}
std::string url_encode(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
std::string url_decode(std::string_view text, bool query) {
    auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%') {
            if (i + 2 >= text.size() || digit(text[i+1]) < 0 || digit(text[i+2]) < 0) throw std::invalid_argument("Malformed URL encoding");
            result += static_cast<char>((digit(text[i+1]) << 4) | digit(text[i+2])); i += 2;
        } else result += (query && text[i] == '+') ? ' ' : text[i];
    }
    if (!valid_utf8(result)) throw std::invalid_argument("Path must be valid UTF-8");
    return result;
}
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        auto c = static_cast<unsigned char>(text[i++]);
        if (c < 128) { if (!c) return false; continue; }
        unsigned count; std::uint32_t value, minimum;
        if ((c & 0xe0) == 0xc0) { count = 1; value = c & 31; minimum = 0x80; }
        else if ((c & 0xf0) == 0xe0) { count = 2; value = c & 15; minimum = 0x800; }
        else if ((c & 0xf8) == 0xf0) { count = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        if (i + count > text.size()) return false;
        while (count--) { const auto d = static_cast<unsigned char>(text[i++]); if ((d & 0xc0) != 0x80) return false; value = (value << 6) | (d & 63); }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
std::string normalize_path(std::string text) {
    if (text.size() > 4096 || !valid_utf8(text)) throw std::invalid_argument("Invalid path");
    if (text.empty()) return "/";
    if (text.find_first_of("\\:\r\n") != std::string::npos) throw std::invalid_argument("Unsupported path component");
    std::string result;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('/', start); if (end == std::string::npos) end = text.size();
        auto part = text.substr(start, end - start);
        if (part == "..") throw std::invalid_argument("Parent traversal is forbidden");
        if (!part.empty() && part != ".") {
            if(part.size()>255) throw std::invalid_argument("Path component is too long");
            auto stem=lower_ascii(part.substr(0,part.find('.')));
            if(stem=="con" || stem=="prn" || stem=="aux" || stem=="nul" || (stem.size()==4 && (stem.substr(0,3)=="com" || stem.substr(0,3)=="lpt") && stem[3]>='1' && stem[3]<='9')) throw std::invalid_argument("Reserved device name");
            if (part.back() == '.' || part.back() == ' ') throw std::invalid_argument("Ambiguous path component");
            for (unsigned char c : part) if (c < 32 || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*') throw std::invalid_argument("Unsupported character in path");
            result += '/'; result += part;
        }
        start = end + 1;
    }
    return result.empty() ? "/" : result;
}
std::map<std::string, std::string> query_params(std::string_view target) {
    std::map<std::string, std::string> result;
    auto at = target.find('?'); if (at == std::string_view::npos) return result;
    target.remove_prefix(at + 1);
    while (!target.empty()) {
        if(result.size()>=32) throw std::invalid_argument("Too many query parameters");
        const auto end = target.find('&'); const auto part = target.substr(0, end);
        const auto eq = part.find('=');
        result[url_decode(part.substr(0, eq), true)] = eq == std::string_view::npos ? "" : url_decode(part.substr(eq + 1), true);
        if (end == std::string_view::npos) break;
        target.remove_prefix(end + 1);
    }
    return result;
}
std::string http_date(std::time_t value) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    std::ostringstream output; output.imbue(std::locale::classic());
    output << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S GMT"); return output.str();
}
std::time_t parse_http_date(const std::string& value) {
    std::tm tm{}; std::istringstream input(value); input.imbue(std::locale::classic());
    input >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    if (input.fail()) return -1;
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}
std::string lower_ascii(std::string text) { for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return text; }
bool frozen_placeholder_name(std::string name) {
    name=lower_ascii(std::move(name));
    return name.rfind(".hls-state-",0)==0 || name.rfind(".hls-config-lock-",0)==0;
}
std::string mime_type(std::string name) {
    auto dot = name.rfind('.'); name = dot == std::string::npos ? "" : lower_ascii(name.substr(dot));
    static const std::map<std::string, std::string> types = {{".html","text/html; charset=utf-8"},{".htm","text/html; charset=utf-8"},{".css","text/css; charset=utf-8"},{".js","text/javascript; charset=utf-8"},{".json","application/json; charset=utf-8"},{".txt","text/plain; charset=utf-8"},{".md","text/plain; charset=utf-8"},{".svg","image/svg+xml"},{".png","image/png"},{".jpg","image/jpeg"},{".jpeg","image/jpeg"},{".gif","image/gif"},{".webp","image/webp"},{".ico","image/x-icon"},{".pdf","application/pdf"},{".zip","application/zip"},{".wasm","application/wasm"},{".mp4","video/mp4"},{".webm","video/webm"},{".mp3","audio/mpeg"},{".woff2","font/woff2"}};
    auto it = types.find(name); return it == types.end() ? "application/octet-stream" : it->second;
}
std::uint64_t parse_unsigned(const std::string& text, std::uint64_t maximum) {
    std::uint64_t value = 0; auto p = std::from_chars(text.data(), text.data() + text.size(), value);
    if (p.ec != std::errc{} || p.ptr != text.data() + text.size() || text.empty() || value > maximum) throw std::invalid_argument("Invalid numeric parameter");
    return value;
}
ByteRange parse_range(std::string_view text, std::uint64_t size) {
    ByteRange r;
    if (text.substr(0, 6) != "bytes=" || text.find(',') != std::string_view::npos) return r;
    text.remove_prefix(6); const auto dash = text.find('-');
    if (dash == std::string_view::npos) return r;
    auto number = [](std::string_view s, std::uint64_t& n) { auto p = std::from_chars(s.data(), s.data()+s.size(), n); return !s.empty() && p.ec == std::errc{} && p.ptr == s.data()+s.size(); };
    auto first = text.substr(0, dash), last = text.substr(dash + 1); std::uint64_t a = 0, b = 0;
    if (first.empty()) {
        if (!number(last, b)) return r;
        if (!b || !size) return {RangeState::unsatisfiable, 0, 0};
        b = std::min(b, size); return {RangeState::valid, size - b, b};
    }
    if (!number(first, a) || (!last.empty() && !number(last, b))) return r;
    if (!last.empty() && b < a) return r;
    if (a >= size) return {RangeState::unsatisfiable, 0, 0};
    b = last.empty() ? size - 1 : std::min(b, size - 1);
    return {RangeState::valid, a, b - a + 1};
}
bool etag_matches(std::string_view values, std::string_view etag) {
    if (etag.substr(0,2) == "W/") etag.remove_prefix(2);
    while (!values.empty()) {
        auto comma = values.find(','); auto item = values.substr(0, comma);
        while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
        while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
        if (item == "*") return true;
        if (item.substr(0,2) == "W/") item.remove_prefix(2);
        if (item == etag) return true;
        if (comma == std::string_view::npos) break;
        values.remove_prefix(comma+1);
    }
    return false;
}
}
