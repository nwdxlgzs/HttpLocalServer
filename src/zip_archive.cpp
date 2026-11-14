// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "zip_archive.hpp"
#include "util.hpp"
#include <boost/beast/zlib/inflate_stream.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <cstring>

namespace hls {
namespace {
using Clock=std::chrono::steady_clock;
[[noreturn]] void bad(const char* reason){throw Failure(422,std::string("ZIP preview rejected: ")+reason);}
void time_limit(Clock::time_point start){if(Clock::now()-start>std::chrono::milliseconds(ZipLimits::milliseconds))bad("processing time limit");}
std::uint16_t u16(const unsigned char* p){return p[0]|(std::uint16_t(p[1])<<8);}
std::uint32_t u32(const unsigned char* p){return u16(p)|(std::uint32_t(u16(p+2))<<16);}
std::uint64_t u64(const unsigned char* p){return u32(p)|(std::uint64_t(u32(p+4))<<32);}
std::vector<unsigned char> read(const std::shared_ptr<File>& f,std::uint64_t at,std::size_t size){
    if(at>f->info().size||size>f->info().size-at)bad("truncated structure");
    std::vector<unsigned char> result(size);if(size&&f->read(at,result.data(),size)!=size)bad("short read");return result;
}
std::string safe_name(const unsigned char* data,std::size_t length,bool utf8){
    if(!length||length>1024)bad("invalid path length");
    std::string s(reinterpret_cast<const char*>(data),length);
    // Unflagged ASCII and UTF-8 are unambiguous. Other legacy encodings must
    // be repacked with UTF-8 instead of guessing a filesystem path.
    if(!valid_utf8(s))bad(utf8?"invalid UTF-8 path":"unsupported legacy path encoding");
    if(s[0]=='/'||s.find('\\')!=std::string::npos||s.find(':')!=std::string::npos)bad("absolute or ambiguous path");
    for(unsigned char c:s)if(c<32||c==127)bad("control character in path");
    unsigned depth=0;std::size_t at=0;
    while(at<s.size()){auto end=s.find('/',at);const auto part=s.substr(at,end==std::string::npos?end:end-at);
        if(part.empty()||part.size()>255||part=="."||part==".."||part.back()=='.'||part.back()==' ')bad("unsafe path component");
        if(++depth>ZipLimits::depth)bad("path depth limit");if(end==std::string::npos)break;at=end+1;}
    return s;
}
std::uint32_t crc_update(std::uint32_t crc,const unsigned char* data,std::size_t size){
    static const auto table=[](){std::array<std::uint32_t,256> v{};for(unsigned n=0;n<256;++n){auto c=n;for(unsigned b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0);v[n]=c;}return v;}();
    for(std::size_t i=0;i<size;++i)crc=table[(crc^data[i])&255]^(crc>>8);return crc;
}
}
ZipArchive::ZipArchive(std::shared_ptr<File> file):file_(std::move(file)){
    const auto start=Clock::now();const auto size=file_->info().size;
    if(size<22||size>ZipLimits::source)bad("archive size limit");
    const auto tail=read(file_,size-std::min<std::uint64_t>(size,65557),static_cast<std::size_t>(std::min<std::uint64_t>(size,65557)));
    std::size_t eocd=tail.size();for(std::size_t i=tail.size()-22;;--i){if(u32(tail.data()+i)==0x06054b50&&i+22+u16(tail.data()+i+20)==tail.size()){eocd=i;break;}if(!i)break;}
    if(eocd==tail.size())bad("missing end record");const auto* e=tail.data()+eocd;
    if(u16(e+4)||u16(e+6)||u16(e+8)!=u16(e+10))bad("multi-volume archive");
    std::uint64_t count=u16(e+10),central_size=u32(e+12),central_offset=u32(e+16),central_end=size-tail.size()+eocd;
    if(count==65535||central_size==0xffffffffu||central_offset==0xffffffffu){
        if(central_end<20)bad("missing ZIP64 locator");const auto locator=read(file_,central_end-20,20);
        if(u32(locator.data())!=0x07064b50||u32(locator.data()+4)||u32(locator.data()+16)!=1)bad("invalid ZIP64 locator");
        const auto pos=u64(locator.data()+8);if(pos>central_end-20||central_end-20-pos<56)bad("invalid ZIP64 end offset");
        const auto z=read(file_,pos,56);if(u32(z.data())!=0x06064b50||u64(z.data()+4)<44||u64(z.data()+4)!=central_end-20-pos-12||u32(z.data()+16)||u32(z.data()+20)||u64(z.data()+24)!=u64(z.data()+32))bad("invalid ZIP64 end record");
        count=u64(z.data()+32);central_size=u64(z.data()+40);central_offset=u64(z.data()+48);central_end=pos;
    }
    if(count>ZipLimits::entries||central_size>ZipLimits::metadata||central_offset>central_end||central_size!=central_end-central_offset)bad("central directory budget or bounds");
    const auto central=read(file_,central_offset,static_cast<std::size_t>(central_size));std::size_t at=0;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> ranges;std::set<std::string> names;
    for(std::uint64_t n=0;n<count;++n){time_limit(start);
        if(at>central.size()||central.size()-at<46)bad("truncated directory entry");const auto* h=central.data()+at;
        if(u32(h)!=0x02014b50)bad("invalid central signature");const auto flags=u16(h+8),method=u16(h+10),name_size=u16(h+28),extra_size=u16(h+30),comment_size=u16(h+32);
        if(flags&~0x080eu)bad("encrypted or unsupported entry flags");if(method!=0&&method!=8)bad("unsupported compression method");
        const std::size_t record=46u+name_size+extra_size+comment_size;if(record>central.size()-at)bad("truncated central name");
        ZipEntry item;item.path=safe_name(h+46,name_size,(flags&2048)!=0);item.directory=item.path.back()=='/';item.method=method;item.crc=u32(h+16);item.compressed=u32(h+20);item.size=u32(h+24);item.offset=u32(h+42);
        auto disk=std::uint64_t(u16(h+34));bool zip64=false;
        for(std::size_t ex=46u+name_size;ex<46u+name_size+extra_size;){
            if(46u+name_size+extra_size-ex<4)bad("truncated extra field");const auto tag=u16(h+ex),len=u16(h+ex+2);ex+=4;
            if(len>46u+name_size+extra_size-ex)bad("extra field outside record");
            if(tag==1){if(zip64)bad("duplicate ZIP64 extra");zip64=true;std::size_t p=0;auto next=[&](){if(len-p<8)bad("truncated ZIP64 value");auto value=u64(h+ex+p);p+=8;return value;};
                if(item.size==0xffffffffu)item.size=next();if(item.compressed==0xffffffffu)item.compressed=next();if(item.offset==0xffffffffu)item.offset=next();if(disk==65535){if(len-p<4)bad("truncated ZIP64 disk");disk=u32(h+ex+p);}}
            ex+=len;
        }
        if(disk||item.size==0xffffffffu||item.compressed==0xffffffffu||item.offset==0xffffffffu)bad("invalid ZIP64 sizes");
        const auto unix_type=(u32(h+38)>>16)&0170000u;if(unix_type&&unix_type!=0100000u&&unix_type!=0040000u)bad("link or special filesystem entry");
        if(item.compressed>ZipLimits::source||item.size>ZipLimits::entry||item.size>ZipLimits::total-expanded_||item.size>std::max<std::uint64_t>(1048576,item.compressed*ZipLimits::ratio))bad("expanded size or compression ratio limit");
        if(item.compressed>ZipLimits::source||(method==0&&item.size!=item.compressed)||(!item.compressed&&item.size)||(item.directory&&item.size))bad("inconsistent entry size");
        expanded_+=item.size;if(item.offset>=central_offset||central_offset-item.offset<30)bad("local header outside payload");
        const auto local=read(file_,item.offset,30);const auto ln=u16(local.data()+26),le=u16(local.data()+28);
        if(u32(local.data())!=0x04034b50||u16(local.data()+6)!=flags||u16(local.data()+8)!=method||ln!=name_size)bad("local and central headers disagree");
        if(std::uint64_t(30)+ln+le>central_offset-item.offset)bad("local metadata exceeds payload");
        const auto local_name=read(file_,item.offset+30,ln);if(std::memcmp(local_name.data(),h+46,ln))bad("local and central paths disagree");
        const auto begin=item.offset;item.offset+=30u+ln+le;
        if(item.compressed>central_offset-item.offset)bad("entry data outside payload");item.end=item.offset+item.compressed;
        if(!(flags&8)){if(u32(local.data()+14)!=item.crc||(u32(local.data()+18)!=0xffffffffu&&u32(local.data()+18)!=item.compressed)||(u32(local.data()+22)!=0xffffffffu&&u32(local.data()+22)!=item.size))bad("local CRC or sizes disagree");}
        else {
            if(central_offset-item.end<12)bad("missing data descriptor");auto descriptor=read(file_,item.end,static_cast<std::size_t>(std::min<std::uint64_t>(24,central_offset-item.end)));std::size_t p=u32(descriptor.data())==0x08074b50?4:0;
            if(descriptor.size()-p<12||u32(descriptor.data()+p)!=item.crc)bad("invalid descriptor CRC");p+=4;
            const bool large=u32(h+20)==0xffffffffu||u32(h+24)==0xffffffffu;
            if(large){if(descriptor.size()-p<16||u64(descriptor.data()+p)!=item.compressed||u64(descriptor.data()+p+8)!=item.size)bad("invalid ZIP64 descriptor");p+=16;}
            else {if(u32(descriptor.data()+p)!=item.compressed||u32(descriptor.data()+p+4)!=item.size)bad("invalid data descriptor");p+=8;}item.end+=p;
        }
        ranges.emplace_back(begin,item.end);auto key=lower_ascii(item.path);if(item.directory)key.pop_back();if(!names.insert(key).second)bad("duplicate or ambiguous path");
        entries_.emplace(item.path,std::move(item));at+=record;
    }
    if(at!=central.size())bad("unexpected central directory data");
    std::sort(ranges.begin(),ranges.end());for(std::size_t i=1;i<ranges.size();++i)if(ranges[i].first<ranges[i-1].second)bad("overlapping entry data");
    for(const auto& pair:entries_){const auto& path=pair.first;for(auto slash=path.find('/');slash!=std::string::npos;slash=path.find('/',slash+1)){const auto found=entries_.find(path.substr(0,slash));if(found!=entries_.end())bad("file and directory collision");}}
    if(!file_->unchanged())throw Failure(409,"ZIP changed during inspection");
}
void ZipArchive::extract(const ZipEntry& entry,const std::function<void(const void*,std::size_t)>& output)const{
    const auto start=Clock::now();if(entry.directory)bad("cannot extract a directory");if(!file_->unchanged())throw Failure(409,"ZIP source changed");
    // musl workers can have an 80 KiB stack: keep stream buffers on the heap.
    std::vector<unsigned char> input(65544),buffer(65536);std::uint64_t consumed=0,produced=0;std::uint32_t crc=0xffffffffu;
    auto emit=[&](std::size_t n){if(n>entry.size-produced)bad("actual output exceeds declared size");crc=crc_update(crc,buffer.data(),n);output(buffer.data(),n);produced+=n;};
    if(!entry.method){while(consumed<entry.compressed){time_limit(start);const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),entry.compressed-consumed));if(file_->read(entry.offset+consumed,buffer.data(),n)!=n)bad("short payload");emit(n);consumed+=n;}}
    else {boost::beast::zlib::inflate_stream inflater;boost::beast::zlib::z_params z{};bool ended=false;
        while(!ended){time_limit(start);if(!z.avail_in&&consumed<entry.compressed){auto n=static_cast<std::size_t>(std::min<std::uint64_t>(65536,entry.compressed-consumed));if(file_->read(entry.offset+consumed,input.data(),n)!=n)bad("short compressed payload");consumed+=n;z.next_in=input.data();z.avail_in=n;if(consumed==entry.compressed){std::fill(input.begin()+n,input.begin()+n+8,0);z.avail_in+=8;}}
            z.next_out=buffer.data();z.avail_out=static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(),entry.size-produced+1));const auto before_in=z.avail_in,before_out=z.avail_out;boost::system::error_code ec;inflater.write(z,boost::beast::zlib::Flush::none,ec);emit(before_out-z.avail_out);
            ended=ec==boost::beast::zlib::error::end_of_stream;if(ec&&!ended&&ec!=boost::beast::zlib::error::need_buffers)bad("invalid deflate stream");if(!ended&&z.avail_in==before_in&&z.avail_out==before_out)bad("truncated or stalled deflate stream");
        }const auto unused=static_cast<unsigned>(z.data_type&63)/8;if(z.total_in<unused||z.total_in-unused!=entry.compressed)bad("compressed payload has trailing data");
    }
    if(produced!=entry.size||(crc^0xffffffffu)!=entry.crc)bad("expanded size or CRC mismatch");if(!file_->unchanged())throw Failure(409,"ZIP changed during extraction");
}
}
