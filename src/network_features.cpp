// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "network_features.hpp"
#include "service.hpp"
#include "util.hpp"
#include "qrcodegen.hpp"
#include <boost/json.hpp>
#include <boost/asio/ip/address.hpp>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hls {
std::string preferred_encoding(const std::string& value){
    int gzip=-1,deflate=-1,wildcard=-1;std::istringstream tokens(lower_ascii(value));std::string token;
    while(std::getline(tokens,token,',')){
        const auto semi=token.find(';');auto name=token.substr(0,semi);name.erase(0,name.find_first_not_of(" \t"));const auto last=name.find_last_not_of(" \t");if(last!=std::string::npos)name.resize(last+1);
        int quality=1000;
        if(semi!=std::string::npos){auto parameters=token.substr(semi+1);parameters.erase(std::remove_if(parameters.begin(),parameters.end(),[](char c){return c==' '||c=='\t';}),parameters.end());
            if(parameters.rfind("q=",0)!=0)quality=0;
            else{const auto q=parameters.substr(2);quality=0;if(q=="1"||q=="1.0"||q=="1.00"||q=="1.000")quality=1000;else if(q=="0")quality=0;else if(q.rfind("0.",0)==0&&q.size()<=5){int scale=100;for(std::size_t i=2;i<q.size();++i){if(q[i]<'0'||q[i]>'9'){quality=0;break;}quality+=(q[i]-'0')*scale;scale/=10;}}}
        }
        if(name=="gzip"||name=="x-gzip")gzip=quality;else if(name=="deflate")deflate=quality;else if(name=="*")wildcard=quality;
    }gzip=gzip>=0?gzip:wildcard;deflate=deflate>=0?deflate:wildcard;return gzip>0&&gzip>=deflate?"gzip":deflate>0?"deflate":"";
}
bool compressible(const std::string& mime){return mime.rfind("text/",0)==0||mime.find("json")!=std::string::npos||mime.find("javascript")!=std::string::npos||mime.find("xml")!=std::string::npos||mime.find("svg")!=std::string::npos||mime.find("wasm")!=std::string::npos;}
bool accepts_gzip(const std::string& value){return preferred_encoding(value)=="gzip";}
GzipEncoder::GzipEncoder(bool deflate):deflate_(deflate){stream_.reset(3,15,8,boost::beast::zlib::Strategy::normal);}
std::string GzipEncoder::write(const void* data,std::size_t size,bool finish){
    namespace zlib=boost::beast::zlib;std::string result;
    if(!started_){if(deflate_)result.assign("\x78\x5e",2);else result.assign("\x1f\x8b\x08\0\0\0\0\0\0\xff",10);started_=true;}
    if(size)crc_.process_bytes(data,size);
    if(deflate_){const auto* p=static_cast<const unsigned char*>(data);for(std::size_t i=0;i<size;++i){adler_a_=(adler_a_+p[i])%65521;adler_b_=(adler_b_+adler_a_)%65521;}}
    size_+=static_cast<std::uint32_t>(size);
    zlib::z_params p{};p.next_in=data;p.avail_in=size;char output[32768];
    for(;;){p.next_out=output;p.avail_out=sizeof(output);boost::system::error_code ec;stream_.write(p,finish?zlib::Flush::finish:zlib::Flush::sync,ec);result.append(output,sizeof(output)-p.avail_out);
        if(ec==zlib::error::end_of_stream)break;
        if(ec&&ec!=zlib::error::need_buffers)throw Failure(500,"Gzip encoding failed");
        if(!p.avail_in&&p.avail_out)break;
    }
    if(finish&&deflate_){const auto sum=(adler_b_<<16)|adler_a_;for(int shift=24;shift>=0;shift-=8)result+=static_cast<char>(sum>>shift);}
    if(finish&&!deflate_){for(auto value:{crc_.checksum(),size_})for(unsigned shift=0;shift<32;shift+=8)result+=static_cast<char>(value>>shift);}
    return result;
}
std::vector<SiteURL> site_urls(const Config& config){
    const bool wildcard=config.bind=="0.0.0.0"||config.bind=="::",ipv6=config.bind.find(':')!=std::string::npos;std::vector<SiteURL> result;
    auto add=[&](std::string address,const std::string& name){
        boost::system::error_code ec;const auto ip=boost::asio::ip::make_address(address,ec);if(ec||ip.is_unspecified()||ip.is_multicast())return;
        int score=ip.is_loopback()?0:50;
        if(ip.is_v4()){const auto b=ip.to_v4().to_bytes();if(b[0]==10||(b[0]==192&&b[1]==168)||(b[0]==172&&b[1]>=16&&b[1]<=31))score=100;if(b[0]==169&&b[1]==254)score=10;}
        else{const auto b=ip.to_v6().to_bytes();if((b[0]&254)==252)score=80;if(ip.to_v6().is_link_local())score=5;}
        const auto label=lower_ascii(name);for(const char* virtual_name:{"virtual","vmware","vethernet","wsl","docker","vbox","tun","vpn","tailscale"})if(label.find(virtual_name)!=std::string::npos){score-=45;break;}
        std::string host;for(char c:address)host+=c=='%'?"%25":std::string(1,c);if(ip.is_v6())host="["+host+"]";
        const auto url=std::string(config.https?"https://":"http://")+host+":"+std::to_string(config.port)+"/";
        if(std::none_of(result.begin(),result.end(),[&](const auto& r){return r.url==url;}))result.push_back({url,name,score});
    };
    if(wildcard){for(const auto& adapter:interface_addresses())if((adapter.address.find(':')!=std::string::npos)==ipv6)add(adapter.address,adapter.name);add(ipv6?"::1":"127.0.0.1","Localhost");}
    else add(config.bind,"Listen address");
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.score>b.score;});if(result.size()>64)result.resize(64);return result;
}
Response urls_response(const Config& config){boost::json::array urls;const auto list=site_urls(config);for(std::size_t i=0;i<list.size();++i)urls.emplace_back(boost::json::object{{"url",list[i].url},{"name",list[i].name},{"recommended",i==0},{"qr","/api/qr?index="+std::to_string(i)}});Response r;r.body=boost::json::serialize(boost::json::object{{"urls",std::move(urls)}});r.headers={{"Cache-Control","no-store"}};return r;}
Response qr_response(const Config& config,const std::string& target){
    const auto parameters=query_params(target);const auto it=parameters.find("index");const auto index=it==parameters.end()?0:parse_unsigned(it->second,63);const auto urls=site_urls(config);if(index>=urls.size())throw Failure(404,"URL is no longer available");
    const auto qr=qrcodegen::QrCode::encodeText(urls[index].url.c_str(),qrcodegen::QrCode::Ecc::MEDIUM);const auto size=qr.getSize();std::ostringstream svg;
    svg<<"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "<<size+8<<' '<<size+8<<"\" shape-rendering=\"crispEdges\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/><path fill=\"black\" d=\"";
    for(int y=0;y<size;++y)for(int x=0;x<size;++x)if(qr.getModule(x,y))svg<<'M'<<x+4<<','<<y+4<<"h1v1h-1z";
    svg<<"\"/></svg>";Response r;r.body=svg.str();r.content_type="image/svg+xml";r.headers={{"Cache-Control","no-store"},{"Content-Security-Policy","default-src 'none'; sandbox"}};return r;
}
void print_cli_qr(const Config& config,std::ostream& output){
    if(!config.show_cli_qr)return;
    const auto urls=site_urls(config);
    for(std::size_t i=0;i<urls.size();++i){const auto qr=qrcodegen::QrCode::encodeText(urls[i].url.c_str(),qrcodegen::QrCode::Ecc::MEDIUM);const auto size=qr.getSize();output<<(i?"Alternate: ":"Recommended: ")<<urls[i].url<<" ("<<urls[i].name<<")\n";
        output.flush();
#ifdef _WIN32
        const auto console=GetStdHandle(STD_OUTPUT_HANDLE);CONSOLE_SCREEN_BUFFER_INFO info{};
        if(GetConsoleScreenBufferInfo(console,&info)){
            // Win7 has no VT escapes. Native colours plus half blocks produce
            // square, scan-ready modules with an actual white quiet zone.
            for(int y=-4;y<size+4;y+=2){for(int x=-4;x<size+4;++x){
                const WORD colour=static_cast<WORD>((qr.getModule(x,y)?0:7)|(qr.getModule(x,y+1)?0:0x70));SetConsoleTextAttribute(console,colour);DWORD written=0;const wchar_t block=0x2580;WriteConsoleW(console,&block,1,&written,nullptr);
            }SetConsoleTextAttribute(console,info.wAttributes);DWORD written=0;WriteConsoleW(console,L"\r\n",2,&written,nullptr);}
            SetConsoleTextAttribute(console,info.wAttributes);continue;
        }
#else
        if(isatty(STDOUT_FILENO)){
            for(int y=-4;y<size+4;y+=2){for(int x=-4;x<size+4;++x)output<<"\033["<<(qr.getModule(x,y)?30:97)<<';'<<(qr.getModule(x,y+1)?40:107)<<"m\xe2\x96\x80";output<<"\033[0m\n";}continue;
        }
#endif
        // Redirected output remains plain text, without terminal control codes.
        for(int y=-4;y<size+4;++y){for(int x=-4;x<size+4;++x)output<<(qr.getModule(x,y)?"##":"  ");output<<'\n';}
    }output<<std::flush;
}
void Service::encode_response(const Request& request,Response& response){
    for(const auto& h:config_.response_headers)response.headers.emplace_back(h.first,h.second);
    if(!config_.compression||response.status!=200||!request.header("range").empty()||!compressible(response.content_type))return;
    for(const auto& header:response.headers)if(lower_ascii(header.first)=="content-encoding")return;
    response.headers.emplace_back("Vary","Accept-Encoding");
    const auto coding=preferred_encoding(request.header("accept-encoding"));if(coding.empty())return;response.deflate_stream=coding=="deflate";
    if(response.file){if(response.length<512)return;response.gzip_stream=true;}
    else{if(response.body.size()<512||response.body.size()>(2ull<<20))return;GzipEncoder encoder(response.deflate_stream);auto data=encoder.write(response.body.data(),response.body.size(),true);if(data.size()>=response.body.size())return;response.body=std::move(data);}
    response.headers.emplace_back("Content-Encoding",coding);
}
Response Service::handle(const Request& request){
    auto response=handle_request(request);
    if(response.deferred){auto deferred=std::move(response.deferred);response.deferred=[this,request,deferred](std::function<void(Response)> done){deferred([this,request,done](Response r){encode_response(request,r);done(std::move(r));});};return response;}
    const auto route=request.target.substr(0,request.target.find('?'));
    try{
        if((!settings()->login_password.empty()||settings()->hotlink_seconds)&&response.status==200&&(route=="/"||route=="/preview"||route=="/_hls/"||route=="/_hls")&&response.content_type.rfind("text/html",0)==0&&access_guard(request)){
            // One cookie per browser, refreshed by opening a page, never by polling.
            new_access(request,response,!settings()->login_password.empty());
        }
        if(response.status<400&&route.rfind("/api/",0)==0&&route!="/api/browse"&&route.rfind("/api/mount/",0)!=0&&route.rfind("/api/p2p/",0)!=0)transfer_activity(request);
        encode_response(request,response);
    }catch(const Failure& e){return error(e.status,e.what());}
    return response;
}
}
