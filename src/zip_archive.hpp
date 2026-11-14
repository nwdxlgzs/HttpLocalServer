// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#pragma once
#include "platform.hpp"
#include <functional>
#include <map>

namespace hls {
// These are safety ceilings, never an invitation to allocate the declared size.
struct ZipLimits {
    static constexpr std::uint64_t source=512ull<<20, entry=32ull<<20, total=256ull<<20;
    static constexpr std::size_t entries=8192, metadata=4u<<20, depth=32;
    static constexpr unsigned ratio=200, milliseconds=2000;
};
struct ZipEntry {
    std::string path;
    std::uint64_t compressed=0,size=0,offset=0,end=0;
    std::uint32_t crc=0;
    unsigned method=0;
    bool directory=false;
};
class ZipArchive {
public:
    explicit ZipArchive(std::shared_ptr<File> file);
    const std::map<std::string,ZipEntry>& entries()const{return entries_;}
    std::uint64_t expanded()const{return expanded_;}
    std::shared_ptr<File> source()const{return file_;}
    void extract(const ZipEntry& entry,const std::function<void(const void*,std::size_t)>& output)const;
private:
    std::shared_ptr<File> file_;
    std::map<std::string,ZipEntry> entries_;
    std::uint64_t expanded_=0;
};
}
