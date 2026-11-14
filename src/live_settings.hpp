// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#pragma once
#include "config.hpp"
#include <memory>
#include <vector>

namespace hls {
// Published as immutable snapshots. Structural budgets and socket settings stay CLI-owned.
struct LiveSettings {
    std::string write_mode="readonly",title="在线文件下载",announcement,attention_click="locate";
    std::vector<std::string> attention_paths;
    bool preview=true,thumbnails=false,magika=true,mcp=false;
    std::string zip_preview="off";
    bool recycle=false;
    unsigned recycle_days=7,ip_transfers=0;
    std::uint64_t recycle_bytes=1024ull<<20,upload_kbps=0,download_kbps=0,ip_upload_kbps=0,ip_download_kbps=0;
    bool p2p=false;
    unsigned p2p_cache_mb=64,p2p_cache_seconds=3600;
    std::string login_password;
    unsigned hotlink_seconds=0;
    std::uint64_t access_revision=1;
    std::size_t ip_limit=256,owner_limit=256;
    std::uint64_t max_upload_bytes=16ull<<30,ip_upload_bytes=0,revision=1;
    std::map<std::string,std::uint64_t> ip_upload_overrides;
    std::uint64_t upload_limit(const std::string& ip)const{const auto found=ip_upload_overrides.find(ip);return found==ip_upload_overrides.end()?ip_upload_bytes:found->second;}
    explicit LiveSettings(const Config& c):write_mode(c.write_mode),title(c.title),announcement(c.announcement),attention_click(c.attention_click),attention_paths(c.attention_paths),preview(c.allow_preview),thumbnails(c.thumbnails),magika(c.magika),mcp(c.mcp),ip_limit(c.ip_limit),owner_limit(c.owner_limit),max_upload_bytes(c.max_upload_bytes),ip_upload_bytes(c.ip_upload_bytes),ip_upload_overrides(c.ip_upload_overrides){zip_preview=c.zip_preview;recycle=c.recycle;recycle_days=c.recycle_days;recycle_bytes=c.recycle_bytes;ip_transfers=c.ip_transfers;upload_kbps=c.upload_kbps;download_kbps=c.download_kbps;ip_upload_kbps=c.ip_upload_kbps;ip_download_kbps=c.ip_download_kbps;login_password=c.login_password;hotlink_seconds=c.hotlink_seconds;p2p=c.p2p;p2p_cache_mb=c.p2p_cache_mb;p2p_cache_seconds=c.p2p_seed_seconds;}
};
}
