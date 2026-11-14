// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "web_assets.hpp"
#include "web_data.hpp"
#include "icon_data.hpp"
#include "preview_data.hpp"
#include <boost/beast/zlib/inflate_stream.hpp>
#include <stdexcept>

namespace hls {
namespace {
std::string unpack_asset(const unsigned char* data,std::size_t bytes,std::size_t raw_size){
    if(bytes<18)throw std::runtime_error("Embedded asset is truncated");
    // Leave a guard byte so a stream ending exactly at the advertised output
    // boundary can consume its final DEFLATE marker without need_buffers.
    std::string output(raw_size+1,'\0');
    boost::beast::zlib::inflate_stream inflater;
    boost::beast::zlib::z_params parameters{};
    parameters.next_in=data+10;parameters.avail_in=bytes-10; // Include the gzip trailer as bounded decoder lookahead.
    parameters.next_out=&output[0];parameters.avail_out=output.size();
    boost::system::error_code ec;
    inflater.write(parameters,boost::beast::zlib::Flush::finish,ec);
    const auto unused=static_cast<unsigned>(parameters.data_type&63)/8;
    if(parameters.total_out!=raw_size||parameters.total_in<unused||parameters.total_in-unused!=bytes-18||ec!=boost::beast::zlib::error::end_of_stream)throw std::runtime_error("Embedded asset decompression failed");
    output.resize(raw_size);return output;
}
}
std::string embedded_page(bool gzip){
    if(gzip)return {reinterpret_cast<const char*>(web_data),sizeof(web_data)};
    // Fallback for HTTP clients without gzip, decoded once from the embedded data.
    static const std::string plain=unpack_asset(web_data,sizeof(web_data),web_raw_size);
    return plain;
}
EmbeddedPreview embedded_preview(const std::string& path,bool gzip){
    for(const auto& item:packed_previews)if(path==item.path){
        if(item.offset>sizeof(preview_data)||item.bytes>sizeof(preview_data)-item.offset||item.raw_bytes>8u*1024u*1024u)throw std::runtime_error("Embedded preview resource exceeds packed bounds");
        const auto* bytes=preview_data+item.offset;
        return {gzip?std::string(reinterpret_cast<const char*>(bytes),item.bytes):unpack_asset(bytes,item.bytes,item.raw_bytes),item.mime,std::string("W/\"hls-preview-")+item.sha+"\""};
    }
    throw std::invalid_argument("Unknown preview resource");
}
EmbeddedIcon embedded_icon(const std::string& path,bool gzip){
    if(path=="/favicon.ico"){
        if(gzip)return {{reinterpret_cast<const char*>(icon_favicon),sizeof(icon_favicon)},"image/x-icon",icon_favicon_etag};
        static const auto plain=unpack_asset(icon_favicon,sizeof(icon_favicon),icon_favicon_raw_size);
        return {plain,"image/x-icon",icon_favicon_etag};
    }
    if(path=="/favicon.svg"){
        if(gzip)return {{reinterpret_cast<const char*>(icon_logo),sizeof(icon_logo)},"image/svg+xml",icon_logo_etag};
        static const auto plain=unpack_asset(icon_logo,sizeof(icon_logo),icon_logo_raw_size);
        return {plain,"image/svg+xml",icon_logo_etag};
    }
    if(path=="/apple-touch-icon.png"){
        if(gzip)return {{reinterpret_cast<const char*>(icon_touch),sizeof(icon_touch)},"image/png",icon_touch_etag};
        static const auto plain=unpack_asset(icon_touch,sizeof(icon_touch),icon_touch_raw_size);
        return {plain,"image/png",icon_touch_etag};
    }
    throw std::invalid_argument("Unknown embedded icon");
}
}
