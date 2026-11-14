// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <algorithm>

namespace hls {
namespace {
using Clock=std::chrono::steady_clock;
namespace j=boost::json;
std::string param(const Request& r,const char* key){const auto p=query_params(r.target);auto i=p.find(key);return i==p.end()?"":i->second;}
constexpr std::uint64_t disk_limit=128ull<<20;
}
void Service::init_zip(){
    std::size_t examined=0;
    for(const auto& item:std::filesystem::directory_iterator(state_.root())){
        if(++examined>16384)break;
        const auto name=item.path().filename().u8string();
        if(name.size()==60&&name.rfind("zip-preview-",0)==0&&std::all_of(name.begin()+12,name.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}))
            try{secure_remove(state_.root(),"/"+name,false,1);}catch(...){std::error_code ec;const auto bytes=std::filesystem::file_size(item.path(),ec);if(!ec)zip_cache_bytes_+=bytes;}
    }
}
void Service::maintain_zip(){
    std::unique_lock<std::mutex> lock(zip_mutex_,std::try_to_lock);if(!lock.owns_lock())return;
    const auto live=settings();const auto now=Clock::now();
    for(auto it=zip_sessions_.begin();it!=zip_sessions_.end();){
        if(live->preview&&live->zip_preview=="host"&&it->second.revision==live->access_revision&&now-it->second.used<std::chrono::seconds(120)){++it;continue;}
        for(auto cached=it->second.cached.begin();cached!=it->second.cached.end();){
            if(!cached->second.active.expired()){++cached;continue;}
            bool removed=false;try{secure_remove(state_.root(),cached->second.path,false,1);removed=true;}catch(const Failure& e){removed=e.status==404;}catch(...){}
            if(removed){zip_cache_bytes_-=cached->second.size;cached=it->second.cached.erase(cached);}else ++cached;
        }
        if(!it->second.cached.empty()){++it;continue;}
        {std::lock_guard<std::mutex> access(access_mutex_);preview_grants_.erase(it->first);}it=zip_sessions_.erase(it);
    }
}
Response Service::zip_dispatch(const Request& request,const std::string& route){
    if(!settings()->preview||settings()->zip_preview!="host")throw Failure(403,"Host ZIP preview is disabled");
    auto cancelled=std::make_shared<std::atomic<bool>>(false);Response r;r.cancel=[cancelled]{cancelled->store(true);};
    r.deferred=[this,request,route,cancelled](std::function<void(Response)> done){
        if(!zip_workers_.submit([this,request,route,cancelled,done]{
            if(cancelled->load()){done(error(499,"ZIP request cancelled"));return;}
            try{done(zip_api(request,route,cancelled));}catch(const Failure& e){done(error(e.status,e.what()));}catch(const std::exception& e){done(error(400,e.what()));}
        })){auto response=error(503,"ZIP preview queue is full");response.headers.emplace_back("Retry-After","1");done(std::move(response));}
    };return r;
}
Response Service::zip_api(const Request& request,const std::string& route,const std::shared_ptr<std::atomic<bool>>& cancelled){
    const auto live=settings();if(!live->preview||live->zip_preview!="host")throw Failure(403,"Host ZIP preview is disabled");
    std::unique_lock<std::mutex> lock(zip_mutex_);
    const bool asset=route.rfind("/zip-assets/",0)==0;
    if(!asset){browser_guard(request,request.method!="GET"&&request.method!="HEAD");if(!access_guard(request,true))throw Failure(401,"Login required");}
    auto limits=[](){return j::object{{"source",ZipLimits::source},{"entry",ZipLimits::entry},{"total",ZipLimits::total},{"entries",ZipLimits::entries},{"ratio",ZipLimits::ratio},{"depth",ZipLimits::depth},{"milliseconds",ZipLimits::milliseconds}};};
    auto result=[&](const std::string& id){Response r;r.body=j::serialize(j::object{{"id",id},{"base","/zip-assets/"+id+"/"},{"limits",limits()}});r.headers={{"Cache-Control","no-store"}};return r;};
    if(route=="/api/zip/open"&&request.method=="POST"){
        j::parse_options options;options.max_depth=4;boost::system::error_code ec;auto input=j::parse(request.body,ec,{},options);
        if(ec||!input.is_object()||!input.as_object().if_contains("path")||!input.at("path").is_string())throw Failure(400,"Expected ZIP path");
        const auto path=normalize_path(std::string(input.at("path").as_string()));protect_config(path);script_file(request,"read",path);
        // A browser mount must first complete its ordinary durable host mirror.
        auto file=open_file(path);unsigned own=0;std::size_t names=0;
        for(auto& session:zip_sessions_){if(session.second.ip==request.peer_ip)++own;for(const auto& name:session.second.archive->entries())names+=name.first.size()+sizeof(ZipEntry)+96;}
        if(own>=4||zip_sessions_.size()>=16||names>12u*1024u*1024u)throw Failure(429,"ZIP preview session budget reached; close a preview");
        auto archive=std::make_shared<ZipArchive>(file);std::size_t added=0;for(const auto& name:archive->entries())added+=name.first.size()+sizeof(ZipEntry)+96;if(added>16u*1024u*1024u-names)throw Failure(507,"ZIP preview index budget reached");
        Response response;const auto sid=new_access(request,response,!live->login_password.empty(),live->access_revision),id=random_token();
        {std::lock_guard<std::mutex> access(access_mutex_);if(preview_grants_.size()>=8192)throw Failure(429,"Preview grant budget reached");preview_grants_[id]={request.peer_ip,sid,live->access_revision,Clock::now(),Clock::now()};}
        ZipSession session;session.ip=request.peer_ip;session.path=path;session.version=file->info().version;session.revision=live->access_revision;session.archive=std::move(archive);session.used=Clock::now();zip_sessions_[id]=std::move(session);
        auto r=result(id);r.headers.insert(r.headers.end(),response.headers.begin(),response.headers.end());return r;
    }
    std::string id=param(request,"id"),entry_path;
    if(asset){const auto slash=route.find('/',12);if(slash==std::string::npos){if(route.size()!=60)throw Failure(400,"Invalid ZIP resource URL");id=route.substr(12);}else{id=route.substr(12,slash-12);entry_path=route.substr(slash+1);}}
    if(id.size()!=48)throw Failure(400,"Invalid ZIP preview capability");auto found=zip_sessions_.find(id);
    if(found==zip_sessions_.end())throw Failure(410,"ZIP preview expired; reopen the archive");auto& session=found->second;
    if(session.ip!=request.peer_ip||session.revision!=live->access_revision)throw Failure(403,"ZIP preview belongs to another client");
    if(Clock::now()-session.used>std::chrono::seconds(120))throw Failure(410,"ZIP preview expired; reopen the archive");
    auto access=request;access.preview_key=id;if(!access_guard(access,true))throw Failure(401,"Login required");
    protect_config(session.path);script_file(request,"read",session.path);
    if(open_file(session.path)->info().version!=session.version||!session.archive->source()->unchanged())throw Failure(409,"ZIP source changed; reopen the archive");session.used=Clock::now();
    if(route=="/api/zip/close"&&request.method=="POST"){
        for(auto cached=session.cached.begin();cached!=session.cached.end();){
            if(!cached->second.active.expired()){++cached;continue;}
            bool removed=false;try{secure_remove(state_.root(),cached->second.path,false,1);removed=true;}catch(const Failure& e){removed=e.status==404;}catch(...){}
            if(removed){zip_cache_bytes_-=cached->second.size;cached=session.cached.erase(cached);}else ++cached;
        }
        if(!session.cached.empty()){session.used=Clock::now()-std::chrono::seconds(121);throw Failure(507,"ZIP cache cleanup pending");}
        {std::lock_guard<std::mutex> access(access_mutex_);preview_grants_.erase(id);}zip_sessions_.erase(found);return result(id);
    }
    if(request.method!="GET"&&request.method!="HEAD")throw Failure(405,"Read-only ZIP preview");
    if(route=="/api/zip/list"){
        const auto directory=normalize_path(param(request,"path"));const auto prefix=directory=="/"?"":directory.substr(1)+"/";
        const auto offset=param(request,"offset").empty()?0:parse_unsigned(param(request,"offset"),ZipLimits::entries);std::map<std::string,ZipEntry> children;
        for(const auto& item:session.archive->entries()){if(item.first.rfind(prefix,0)!=0||item.first.size()<=prefix.size())continue;const auto end=item.first.find('/',prefix.size());const auto path=end==std::string::npos?item.first:item.first.substr(0,end+1);if(children.count(path))continue;auto entry=item.second;entry.path=path;if(end!=std::string::npos){entry.directory=true;entry.size=0;}children.emplace(path,std::move(entry));}
        j::array entries;std::size_t i=0;for(const auto& child:children){if(i++<offset)continue;if(entries.size()>=100)break;auto name=child.first.substr(prefix.size());if(!name.empty()&&name.back()=='/')name.pop_back();entries.emplace_back(j::object{{"name",name},{"path","/"+child.first},{"directory",child.second.directory},{"size",child.second.size},{"mime",extension_type(name).mime}});}
        Response r;r.body=j::serialize(j::object{{"total",children.size()},{"offset",offset},{"entries",std::move(entries)},{"expanded",session.archive->expanded()}});r.headers={{"Cache-Control","no-store"}};return r;
    }
    if(!asset)throw Failure(404,"Unknown ZIP preview operation");
    const auto raw_path=request.target.substr(0,request.target.find('?'));
    if(entry_path.empty()||raw_path.back()=='/')entry_path+=(entry_path.empty()?"":"/")+std::string("index.html");
    else if(session.archive->entries().count(entry_path+"/index.html")){Response r;r.status=302;r.headers={{"Location",raw_path+"/"},{"Cache-Control","no-store"}};return r;}
    const auto entry=session.archive->entries().find(entry_path);if(entry==session.archive->entries().end()||entry->second.directory)throw Failure(404,"ZIP entry not found");
    auto cached=session.cached.find(entry_path);
    if(cached==session.cached.end()){
        if(session.cached.size()>=512||entry->second.size>disk_limit-std::min(disk_limit,zip_cache_bytes_)||session.work>ZipLimits::total-entry->second.size)throw Failure(507,"ZIP expanded cache or work budget reached");
        session.work+=entry->second.size;const auto path="/zip-preview-"+random_token();Upload output(state_.root(),path,ZipLimits::entry);
        session.archive->extract(entry->second,[&](const void* bytes,std::size_t count){if(cancelled->load())throw Failure(499,"ZIP request cancelled");output.append(bytes,count);});output.prepare();output.commit();
        zip_cache_bytes_+=entry->second.size;cached=session.cached.emplace(entry_path,ZipSession::Cached{path,entry->second.size}).first;
    }
    Response r;r.file=cached->second.active.lock();if(!r.file){r.file=File::open(state_.root(),cached->second.path);cached->second.active=r.file;}r.length=entry->second.size;const auto detected=identify(r.file,entry_path);r.content_type=detected.mime;
    r.headers={{"Cache-Control","no-store"},{"Accept-Ranges","bytes"},{"X-Content-Type-Options","nosniff"},{"Referrer-Policy","no-referrer"},{"Access-Control-Allow-Origin","null"},{"Cross-Origin-Resource-Policy","cross-origin"}};
    r.headers.emplace_back("X-HLS-Type-Extension",detected.extension);
    const auto agent=lower_ascii(request.header("user-agent"));
    const bool attachment=param(request,"download")=="1"||((agent.find("trident/")!=std::string::npos||agent.find("msie ")!=std::string::npos)&&(r.content_type.rfind("text/html",0)==0||r.content_type.find("svg")!=std::string::npos||r.content_type.find("xml")!=std::string::npos));
    if(attachment)
        r.headers.emplace_back("Content-Disposition","attachment; filename=\"download\"; filename*=UTF-8''"+url_encode(entry_path.substr(entry_path.find_last_of('/')+1)));
    const auto host_name=lower_ascii(request.header("host"));const auto origin=std::string(request.secure?"https://":"http://")+((request.control_channel||host_name==lower_ascii(request.authority)||host_name=="localhost:"+std::to_string(config_.port)||host_name=="localhost"&&config_.port==(request.secure?443:80))?host_name:request.authority);const auto prefix="/zip-assets/"+id+"/";
    r.headers.emplace_back("Content-Security-Policy","sandbox allow-scripts; default-src "+origin+prefix+" data: blob:; script-src "+origin+prefix+" 'unsafe-inline'; style-src "+origin+prefix+" 'unsafe-inline'; connect-src "+origin+prefix+"; object-src 'none'; frame-src 'none'; form-action 'none'; base-uri "+origin+prefix+"; worker-src 'none'");
    const bool text=r.content_type.rfind("text/",0)==0||r.content_type.find("javascript")!=std::string::npos||r.content_type.find("svg")!=std::string::npos;
    if(text&&!attachment&&r.length<=(2u<<20)&&request.header("range").empty()){
        std::string raw(static_cast<std::size_t>(r.length),'\0');if(!raw.empty())r.file->read(0,raw.data(),raw.size());
        for(std::size_t i=0;i<raw.size();++i){const char c=raw[i];r.body+=c;if((c=='\''||c=='\"'||(c=='('&&i>=3&&lower_ascii(raw.substr(i-3,3))=="url"))&&i+1<raw.size()&&raw[i+1]=='/'&&(i+2==raw.size()||(raw[i+2]!='/'&&raw[i+2]!='>')))r.body+=prefix.substr(0,prefix.size()-1);if(r.body.size()>4u*1024u*1024u)throw Failure(413,"ZIP resource rewrite budget reached");}r.file.reset();r.length=0;
    }else {const auto range=parse_range(request.header("range"),r.length);if(range.state==RangeState::unsatisfiable){auto e=error(416,"ZIP resource range outside bounds");e.headers.emplace_back("Content-Range","bytes */"+std::to_string(r.length));return e;}if(range.state==RangeState::valid){r.status=206;r.offset=range.first;r.length=range.length;r.headers.emplace_back("Content-Range","bytes "+std::to_string(range.first)+"-"+std::to_string(range.first+range.length-1)+"/"+std::to_string(entry->second.size));}}
    return r;
}
}
