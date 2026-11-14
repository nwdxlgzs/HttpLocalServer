// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include <boost/beast/zlib/deflate_stream.hpp>
#include <boost/crc.hpp>
#include <string>
#include <vector>
#include <iosfwd>
namespace hls {
struct Response;
std::string preferred_encoding(const std::string& value);
bool accepts_gzip(const std::string& value);
bool compressible(const std::string& mime);
class GzipEncoder {
    boost::beast::zlib::deflate_stream stream_;
    boost::crc_32_type crc_;
    std::uint32_t size_=0;bool started_=false;bool deflate_=false;std::uint32_t adler_a_=1,adler_b_=0;
public:
    explicit GzipEncoder(bool deflate=false);
    std::string write(const void* data,std::size_t size,bool finish);
};
struct SiteURL {std::string url,name;int score=0;};
std::vector<SiteURL> site_urls(const Config& config);
Response urls_response(const Config& config);
Response qr_response(const Config& config,const std::string& target);
void print_cli_qr(const Config& config,std::ostream& output);
}
