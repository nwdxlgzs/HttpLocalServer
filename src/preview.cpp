// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <algorithm>

namespace hls {
Response Service::preview_asset(const Request& request,const std::string& path,bool explicit_view){
    if(!settings()->preview)throw Failure(403,"Preview is disabled");
    auto logical=explicit_view?normalize_path(path.substr(5)):path;
    const auto raw_path=request.target.substr(0,request.target.find('?'));
    const auto resolved=resolve(logical);
    if(std::filesystem::is_directory(resolved)){
        if(raw_path.back()!='/'){Response response;response.status=302;response.headers.emplace_back("Location",request.preview_key.empty()?raw_path+"/":"/preview-assets/"+request.preview_key+raw_path.substr(5)+"/");return response;}
        logical=logical=="/"?"/index.html":logical+"/index.html";
    }
    const auto parameters=query_params(request.target);const auto version=parameters.find("version");
    auto response=download(request,logical,false,version==parameters.end()?"":version->second);
    // Opaque sandbox origins intentionally do not receive login cookies.
    // Relative assets stay below a short-lived, read-only preview capability;
    // rewrite root URLs in bounded textual resources to preserve that scope.
    const bool textual=response.content_type.rfind("text/html",0)==0||response.content_type.rfind("text/css",0)==0||response.content_type.find("javascript")!=std::string::npos||response.content_type.find("svg")!=std::string::npos;
    if(!request.preview_key.empty()&&textual&&response.status==200&&response.file&&response.length<=(2ull<<20)&&request.header("range").empty()){
        std::string original(static_cast<std::size_t>(response.length),'\0');if(!original.empty())response.file->read(0,&original[0],original.size());
        std::string output;output.reserve(original.size()+1024);const auto prefix="/preview-assets/"+request.preview_key;
        for(std::size_t i=0;i<original.size();++i){
            const char c=original[i];output+=c;
            const bool url_bracket=c=='('&&i>=3&&lower_ascii(original.substr(i-3,3))=="url";
            const bool root=(c=='\''||c=='\"'||url_bracket)&&i+1<original.size()&&original[i+1]=='/'&&(i+2==original.size()||(original[i+2]!='/'&&original[i+2]!='>'));
            if(root){if(output.size()+prefix.size()+original.size()-i>(8ull<<20))throw Failure(413,"Preview resource rewrite exceeds budget");output+=prefix;}
        }
        response.file.reset();response.body=std::move(output);response.length=0;
        response.headers.erase(std::remove_if(response.headers.begin(),response.headers.end(),[](const auto& h){return h.first=="ETag"||h.first=="Last-Modified"||h.first=="Accept-Ranges"||h.first=="Cache-Control";}),response.headers.end());
        response.headers.emplace_back("Cache-Control","no-store");
    }
    // Shared pages execute under an opaque sandbox origin. Related static files
    // are readable from that origin; management APIs never receive this CORS grant.
    response.headers.emplace_back("Access-Control-Allow-Origin","null");
    response.headers.emplace_back("Access-Control-Expose-Headers","Content-Length,Content-Range,Accept-Ranges");
    response.headers.emplace_back("Cross-Origin-Resource-Policy","cross-origin");
    const auto host=lower_ascii(request.header("host"));
    const bool local_name=host=="localhost:"+std::to_string(config_.port)||(config_.port==(request.secure?443:80)&&host=="localhost");
    const auto source=std::string(request.secure?"https://":"http://")+(host==lower_ascii(request.authority)||local_name?host:request.authority);
    response.headers.emplace_back("Content-Security-Policy","sandbox allow-scripts; default-src "+source+" data: blob:; script-src "+source+" 'unsafe-inline' 'unsafe-eval'; style-src "+source+" 'unsafe-inline'; connect-src "+source+"; object-src 'none'; form-action 'none'; base-uri "+source);
    const auto ua=lower_ascii(request.header("user-agent"));
    if(ua.find("trident/")!=std::string::npos||ua.find("msie ")!=std::string::npos){
        if(response.content_type.rfind("text/html",0)==0||response.content_type.rfind("image/svg",0)==0||response.content_type.find("xml")!=std::string::npos||response.content_type=="application/pdf")
            response.headers.emplace_back("Content-Disposition","attachment; filename=\"download\"; filename*=UTF-8''"+url_encode(std::filesystem::u8path(logical).filename().u8string()));
    }
    return response;
}
}
