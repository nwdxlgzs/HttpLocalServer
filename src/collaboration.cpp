// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <boost/asio/ip/address.hpp>
#include <algorithm>

namespace hls {
namespace {
namespace json=boost::json;
using Clock=std::chrono::steady_clock;
bool within(const std::string& root,const std::string& path){return path==root||(path.size()>root.size()&&path.compare(0,root.size(),root)==0&&path[root.size()]=='/');}
std::string owner_part(const std::string& path){
    if(path.compare(0,10,"/_clients/")!=0)return {};
    const auto end=path.find('/',10);return path.substr(0,end);
}
bool owner_name(std::string name){
    if(name.compare(0,3,"v6-")==0){
        auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        if(name.size()%2!=1||name.size()>131)return false;
        std::string decoded;for(std::size_t i=3;i<name.size();i+=2){const int a=hex(name[i]),b=hex(name[i+1]);if(a<0||b<0)return false;decoded+=static_cast<char>((a<<4)|b);}name=decoded;
    }
    boost::system::error_code ec;boost::asio::ip::make_address(name,ec);return !ec;
}
bool browser_id(const std::string& id){return id.size()>=16&&id.size()<=64&&std::all_of(id.begin(),id.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='z')||c=='-';});}
json::object body_object(const Request& request,std::size_t maximum=8192){
    if(request.body.size()>maximum)throw Failure(413,"Request document is too large");
    boost::system::error_code ec;json::parse_options options;options.max_depth=4;
    const auto value=json::parse(request.body,ec,{},options);
    if(ec||!value.is_object())throw Failure(400,"Expected a JSON object");
    return value.as_object();
}
std::string string_field(const json::object& object,const char* key,std::size_t limit=4096){
    const auto* value=object.if_contains(key);if(!value||!value->is_string()||value->as_string().size()>limit)throw Failure(400,std::string("Invalid field: ")+key);
    return std::string(value->as_string());
}
Response encoded(json::object value){Response result;result.body=json::serialize(value);result.headers={{"Cache-Control","no-store"}};return result;}
// Only rmdir empty directories. Never recursively delete files, even if an
// external writer creates one between enumeration and the final removal.
bool prune_empty(const std::filesystem::path& root,const std::string& logical,std::size_t& budget,unsigned depth,bool& removed){
    if(!budget||depth>64)return false;
    --budget;
    try{
        const auto resolved=resolve_path(root,logical);std::error_code ec;
        const auto state=std::filesystem::symlink_status(resolved,ec);
        if(ec||!std::filesystem::is_directory(state)||link_like(resolved))return false;
        bool empty=true;
        std::filesystem::directory_iterator it(resolved,ec),end;if(ec)return false;
        while(it!=end){
            if(!budget)return false;
            const auto child=*it;it.increment(ec);if(ec)return false;
            const auto status=child.symlink_status(ec);
            if(ec||!std::filesystem::is_directory(status)||link_like(child.path()))return false;
            if(!prune_empty(root,logical+"/"+child.path().filename().u8string(),budget,depth+1,removed))empty=false;
        }
        if(!empty)return false;
        secure_remove(root,logical,false,1);removed=true;return true;
    }catch(const std::exception&){return false;}
}
}
bool Service::virtual_owner(const Request& request,const std::string& path)const{
    return settings()->write_mode=="isolated"&&(path==owner_root(request)||path=="/_clients");
}
void Service::upload_parents(const Request& request,const std::string& path,bool recursive){
    if(std::count(path.begin(),path.end(),'/')>65)throw Failure(400,"Upload directory depth exceeds 64");
    const auto own=owner_root(request);
    if(settings()->write_mode=="isolated"&&within(own,path)&&mapped(path).root==config_.root)ensure_owner(request);
    if(!recursive)return;
    // The complete path has already been normalized and authorized. Each
    // component is still opened without following links by secure_mkdir.
    for(auto slash=path.find('/',1);slash!=std::string::npos;slash=path.find('/',slash+1)){
        const auto directory=path.substr(0,slash);
        if(settings()->write_mode=="isolated"&&directory=="/_clients")continue;
        if(settings()->write_mode=="isolated"&&directory==own)continue;
        try{if(std::filesystem::is_directory(resolve(directory)))continue;}catch(const Failure& e){if(e.status!=404)throw;}
        authorize(request,directory);mkdir_path(directory,true);
    }
}
void Service::note_browse(const Request& request,const std::string& path,const std::string& id){

    const auto now=Clock::now();std::lock_guard<std::mutex> lock(browse_mutex_);
    const auto key=request.peer_ip+":"+(browser_id(id)?id:"legacy");
    auto found=browsers_.find(key);
    if(found==browsers_.end()){
        if(browsers_.size()>=2048)throw Failure(503,"Browser state capacity reached");
        std::size_t count=0;
        for(const auto& item:browsers_)if(item.second.ip==request.peer_ip&&++count>=16)throw Failure(429,"Too many browser pages for this IP");
        found=browsers_.emplace(key,BrowserLease{request.peer_ip,{},now}).first;
    }
    auto& lease=found->second;lease.expires=now+std::chrono::seconds(45);
    std::size_t bytes=path.size();for(const auto& item:lease.paths)bytes+=item.size();
    if(bytes<=12288&&lease.paths.size()<12&&std::find(lease.paths.begin(),lease.paths.end(),path)==lease.paths.end())lease.paths.push_back(path);
}
std::string Service::directory_stamp(const std::string& path,bool hidden){
    const auto now=Clock::now();const auto key=path+(hidden?"\nall":"\nvisible");
    std::lock_guard<std::mutex> lock(watch_mutex_);auto found=watches_.find(key);
    if(found!=watches_.end()&&now-found->second.checked<std::chrono::seconds(5))return found->second.stamp;
    // Shared across all visitors of a directory. Commutative accumulation
    // avoids relying on filesystem enumeration order or retaining filenames.
    std::uint64_t sum=0,xored=0,count=0;bool exists=false,truncated=false;
    try{
        const auto resolved=resolve(path);std::error_code ec;
        exists=std::filesystem::is_directory(resolved,ec);
        if(exists){
            std::filesystem::directory_iterator it(resolved,ec),end;
            if(ec)throw Failure(403,"Cannot inspect directory");
            for(;it!=end;it.increment(ec)){
                if(ec)break;
                const auto entry=*it;const auto state=entry.symlink_status(ec);
                if(ec||std::filesystem::is_symlink(state)||link_like(entry.path()))continue;
                const bool frozen=config_store_.frozen(entry.path());
                if(!hidden&&(frozen||entry_hidden(entry.path())))continue;
                const auto name=entry.path().filename().u8string();
                if(++count>config_.directory_limit){truncated=true;break;}
                std::uint64_t hash=1469598103934665603ull;
                auto mix=[&](std::uint64_t value){for(unsigned i=0;i<8;++i){hash^=value&255;hash*=1099511628211ull;value>>=8;}};
                for(unsigned char c:name){hash^=c;hash*=1099511628211ull;}
                mix(static_cast<unsigned>(state.type()));
                if(!frozen){
                    const auto time=entry.last_write_time(ec);if(!ec)mix(static_cast<std::uint64_t>(time.time_since_epoch().count()));
                    if(std::filesystem::is_regular_file(state)){const auto bytes=entry.file_size(ec);if(!ec)mix(bytes);}
                }
                sum+=hash;xored^=hash;
            }
        }
    }catch(const Failure& e){if(e.status!=404)throw;}
    std::vector<DirectoryEntry> mounted;mount_entries(path,mounted);for(const auto& entry:mounted){if(!hidden&&(entry.hidden||frozen(entry.path)))continue;++count;for(unsigned char c:entry.name){sum=sum*131+c;}}
    const auto stamp=std::to_string(exists)+":"+std::to_string(count)+":"+std::to_string(sum)+":"+std::to_string(xored)+":"+std::to_string(truncated);
    if(watches_.size()>=128&&found==watches_.end()){
        const auto oldest=std::min_element(watches_.begin(),watches_.end(),[](const auto& a,const auto& b){return a.second.checked<b.second.checked;});watches_.erase(oldest);
    }
    watches_[key]={stamp,Clock::now()};return stamp;
}
Response Service::browse(const Request& request){
    browser_guard(request,true);if(request.method!="POST")throw Failure(405,"Use POST for browser state");
    const auto object=body_object(request,32768);const auto id=string_field(object,"id",64);
    if(!browser_id(id))throw Failure(400,"Invalid browser ID");
    const auto* paths=object.if_contains("paths");if(!paths||!paths->is_array()||paths->as_array().size()>12)throw Failure(400,"At most 12 directory paths are accepted");
    std::vector<std::string> normalized;std::size_t bytes=0;
    for(const auto& item:paths->as_array()){
        if(!item.is_string())throw Failure(400,"Expected directory paths");
        const auto path=normalize_path(std::string(item.as_string()));bytes+=path.size();
        if(bytes>12288)throw Failure(400,"Browser paths exceed the budget");
        normalized.push_back(path);
    }
    const auto current=normalize_path(string_field(object,"current"));
    std::vector<std::string> owners;
    if(const auto* requested=object.if_contains("owners")){
        if(!requested->is_array()||requested->as_array().size()>64)throw Failure(400,"At most 64 visible owners are accepted");
        for(const auto& item:requested->as_array()){
            if(!item.is_string()||item.as_string().size()>160)throw Failure(400,"Invalid owner path");
            const auto path=normalize_path(std::string(item.as_string()));if(owner_part(path)!=path||!owner_name(path.substr(10)))throw Failure(400,"Invalid owner path");owners.push_back(path);
        }
    }
    const auto* hidden=object.if_contains("showHidden");if(hidden&&!hidden->is_bool())throw Failure(400,"Invalid hidden preference");
    {
        std::lock_guard<std::mutex> lock(browse_mutex_);const auto key=request.peer_ip+":"+id;
        if(normalized.empty())browsers_.erase(key);
        else{
            auto found=browsers_.find(key);
            if(found==browsers_.end()){
                if(browsers_.size()>=2048)throw Failure(503,"Browser state capacity reached");
                std::size_t count=0;
                for(const auto& item:browsers_)if(item.second.ip==request.peer_ip&&++count>=16)throw Failure(429,"Too many browser pages for this IP");
            }
            browsers_[key]={request.peer_ip,normalized,Clock::now()+std::chrono::seconds(45)};
        }
    }
    json::object result{{"stamp",normalized.empty()||remote_mount(current)?"":directory_stamp(current,hidden&&hidden->as_bool())},{"interval",12000},{"site",json::parse(site_settings().body)},{"mounts",json::parse(mounts_json())}};
    {std::lock_guard<std::mutex> lock(alias_mutex_);json::object names;for(const auto& path:owners){const auto found=aliases_.find(path);names[path]=found==aliases_.end()?"":found->second.approved;}result["names"]=std::move(names);result["aliasRevision"]=alias_revision_.load();
        if(request.local_admin){std::size_t pending=0;for(const auto& item:aliases_)if(!item.second.pending.empty())++pending;result["pendingNames"]=pending;}
    }
    Mount mounted;if(!normalized.empty()&&remote_mount(current,&mounted)){
        const auto relative=current==mounted.path?"/":current.substr(mounted.path.size());
        return remote_call(mounted.path,json::serialize(json::object{{"op","stamp"},{"path",relative},{"showHidden",hidden&&hidden->as_bool()}}),"",[result=std::move(result)](Response response)mutable{if(response.status>=400)return response;result["stamp"]=json::parse(response.body).as_object().at("stamp");return encoded(std::move(result));});
    }
    return encoded(std::move(result));
}
void Service::maintain_browsing(){
    const auto now=Clock::now();
    std::unique_lock<std::mutex> write(write_mutex_,std::try_to_lock);if(!write.owns_lock())return;
    std::lock_guard<std::mutex> lease_lock(browse_mutex_);
    for(auto it=browsers_.begin();it!=browsers_.end();)if(it->second.expires<now)it=browsers_.erase(it);else ++it;
    if(settings()->write_mode!="isolated")return;
    try{
        std::error_code ec;const std::filesystem::directory_iterator end;
        if(cleanup_iterator_==end)cleanup_iterator_=std::filesystem::directory_iterator(resolve_path(config_.root,"/_clients"),ec);
        if(ec)return;
        bool removed=false;
        // A bounded maintenance slice; a full share scan never runs per visitor.
        std::size_t budget=512;
        for(unsigned i=0;i<32&&budget&&cleanup_iterator_!=end;++i){
            const auto entry=*cleanup_iterator_;cleanup_iterator_.increment(ec);if(ec){cleanup_iterator_=end;break;}
            const auto name=entry.path().filename().u8string();if(!owner_name(name))continue;
            const auto own="/_clients/"+name;bool busy=false;
            for(const auto& lease:browsers_)for(const auto& path:lease.second.paths)if(within(own,path))busy=true;
            for(auto it=uploads_.begin();it!=uploads_.end();)if(it->second.expired())it=uploads_.erase(it);else{if(within(own,it->first))busy=true;++it;}
            if(!busy)prune_empty(config_.root,own,budget,0,removed);
        }
        if(removed)changed();
    }catch(const std::exception&){}
}
Response Service::decorate_entries(Response response){
    auto document=json::parse(response.body);auto& entries=document.as_object().at("entries").as_array();
    std::lock_guard<std::mutex> lock(alias_mutex_);
    for(auto& entry:entries){auto& object=entry.as_object();const auto own=owner_part(std::string(object.at("path").as_string()));
        const auto found=aliases_.find(own);if(found!=aliases_.end()&&!found->second.approved.empty())object["ownerName"]=found->second.approved;
        if(frozen(std::string(object.at("path").as_string()))){object["frozen"]=true;object["writable"]=false;object["size"]=0;object["modified"]=0;object["type"]=json::object{{"label","冻结的主机配置"},{"category","frozen"},{"mime","application/octet-stream"},{"confidence","protected"}};}
    }
    document.as_object()["aliasRevision"]=alias_revision_.load();response.body=json::serialize(document);return response;
}
Response Service::aliases(const Request& request,bool admin){
    if(admin&&!request.local_admin)throw Failure(403,"Only the host may review names");
    browser_guard(request,request.method!="GET");
    if(request.method!="GET"&&request.method!="POST")throw Failure(405,"Use GET or POST for names");
    if(!admin&&settings()->write_mode!="isolated")throw Failure(403,"Names are available in isolated mode");
    const auto own=owner_root(request);
    json::object object;if(request.method=="POST")object=body_object(request);
    std::lock_guard<std::mutex> lock(alias_mutex_);
    if(request.method=="POST"){
        if(admin){
            const auto path=string_field(object,"path"),revision=string_field(object,"revision",32),action=string_field(object,"action",16);
            const auto found=aliases_.find(path);if(found==aliases_.end()||found->second.pending.empty())throw Failure(404,"Pending name not found");
            auto& value=found->second;if(revision!=std::to_string(value.revision))throw Failure(409,"Name was resubmitted; review the latest request");
            if(action=="approve")value.approved=value.pending;else if(action!="reject")throw Failure(400,"Unknown review action");
            value.pending.clear();++value.revision;++alias_revision_;
        }else{
            const auto found=aliases_.find(own);if(found==aliases_.end()||!found->second.uploaded)throw Failure(403,"Upload a file before submitting a name");
            auto name=string_field(object,"name",128);const auto first=name.find_first_not_of(" \t"),last=name.find_last_not_of(" \t");name=first==std::string::npos?"":name.substr(first,last-first+1);
            if(name.empty()||!valid_utf8(name))throw Failure(400,"Please enter a name");
            for(unsigned char c:name)if(c<32||c==127)throw Failure(400,"Control characters are not permitted in names");
            for(const auto* forbidden:{"\xe2\x80\xaa","\xe2\x80\xab","\xe2\x80\xac","\xe2\x80\xad","\xe2\x80\xae","\xe2\x81\xa6","\xe2\x81\xa7","\xe2\x81\xa8","\xe2\x81\xa9"})if(name.find(forbidden)!=std::string::npos)throw Failure(400,"Directional controls are not permitted in names");
            auto& value=found->second;if(value.pending==name||value.approved==name)return encoded({{"submitted",true}});
            if(Clock::now()-value.submitted<std::chrono::seconds(3))throw Failure(429,"Please wait before submitting again");
            value.pending=name;value.submitted=Clock::now();++value.revision;
        }
    }
    if(request.method=="POST"){if(admin){const auto path=string_field(object,"path");persist_alias(path,&aliases_.at(path));}else persist_alias(own,&aliases_.at(own));}
    if(admin){
        json::array entries;for(const auto& item:aliases_)if(!item.second.pending.empty())entries.push_back(json::object{{"path",item.first},{"ip",item.first.substr(10)},{"name",item.second.pending},{"approved",item.second.approved},{"revision",std::to_string(item.second.revision)}});
        return encoded({{"entries",std::move(entries)}});
    }
    const auto found=aliases_.find(own);if(found==aliases_.end())return encoded({{"eligible",false}});
    return encoded({{"eligible",found->second.uploaded},{"name",found->second.approved},{"pending",found->second.pending}});
}
}
