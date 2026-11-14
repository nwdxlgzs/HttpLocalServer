// SPDX-License-Identifier: Apache-2.0
#include "script_policy.hpp"
#include <array>
#include <boost/asio/ip/address.hpp>
#include <charconv>
namespace hls {
namespace {
struct Network {
    std::array<unsigned char, 16> bytes{};
    unsigned bits = 0, width = 0;
};
Network network(const std::string &text) {
    const auto slash = text.find('/');
    boost::system::error_code ec;
    const auto address = boost::asio::ip::make_address(text.substr(0, slash), ec);
    if (ec || address.is_unspecified() && slash == std::string::npos)
        throw 0;
    Network n;
    n.width = address.is_v4() ? 32 : 128;
    n.bits = n.width;
    if (address.is_v4()) {
        auto bytes = address.to_v4().to_bytes();
        std::copy(bytes.begin(), bytes.end(), n.bytes.begin());
    } else {
        if (address.to_v6().scope_id())
            throw 0;
        n.bytes = address.to_v6().to_bytes();
    }
    if (slash != std::string::npos) {
        auto suffix = text.substr(slash + 1);
        auto p = std::from_chars(suffix.data(), suffix.data() + suffix.size(), n.bits);
        if (p.ec != std::errc{} || p.ptr != suffix.data() + suffix.size() || n.bits > n.width)
            throw 0;
    }
    return n;
}
} // namespace
bool valid_ip_rule(const std::string &rule) {
    try {
        network(rule);
        return true;
    } catch (...) {
        return false;
    }
}
bool matches_ip_rule(const std::string &ip, const std::string &rule) {
    try {
        auto a = network(ip), b = network(rule);
        if (a.width != b.width)
            return false;
        for (unsigned i = 0; i < b.bits; ++i)
            if ((a.bytes[i / 8] & (128 >> (i % 8))) != (b.bytes[i / 8] & (128 >> (i % 8))))
                return false;
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace hls
