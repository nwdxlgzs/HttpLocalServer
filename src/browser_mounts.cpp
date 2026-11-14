// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <algorithm>

namespace hls {
void Service::charge_upload(const std::string& ip,std::size_t bytes){
    std::lock_guard<std::mutex> lock(quota_->mutex);if(quota_->used.size()>=65536&&!quota_->used.count(ip))throw Failure(503,"Upload accounting capacity reached");
    auto& used=quota_->used[ip];const auto reserved=quota_->reserved[ip],limit=settings()->upload_limit(ip);
    if(bytes>UINT64_MAX-used||reserved>UINT64_MAX-used-bytes||(limit&&(used>limit||reserved>limit-used||bytes>limit-used-reserved)))throw Failure(413,"This IP has exhausted its upload allowance");
    state_.put("quota",ip,std::to_string(used+bytes));used+=bytes;
}
std::function<void(std::size_t)> Service::upload_charge(const Request& request){
    if(request.prepaid||(settings()->write_mode=="isolated"&&request.local_admin))return {};
    const auto ip=request.peer_ip;
    if(request.header("content-length").empty())return [this,ip](std::size_t bytes){charge_upload(ip,bytes);};
    const auto length=parse_unsigned(request.header("content-length"),UINT64_MAX);
    std::lock_guard<std::mutex> lock(quota_->mutex);if(quota_->used.size()>=65536&&!quota_->used.count(ip))throw Failure(503,"Upload accounting capacity reached");
    const auto used=quota_->used[ip],reserved=quota_->reserved[ip],limit=settings()->upload_limit(ip);
    if(length>UINT64_MAX-used||reserved>UINT64_MAX-used-length||(limit&&(used>limit||reserved>limit-used||length>limit-used-reserved)))throw Failure(413,"This IP has insufficient upload allowance remaining");
    quota_->reserved[ip]+=length;
    auto remaining=std::shared_ptr<std::uint64_t>(new std::uint64_t(length),[account=quota_,ip](std::uint64_t* value){std::lock_guard<std::mutex> guard(account->mutex);account->reserved[ip]-=*value;delete value;});
    return [this,ip,remaining](std::size_t bytes){std::lock_guard<std::mutex> guard(quota_->mutex);const auto limit=settings()->upload_limit(ip),used=quota_->used[ip];if(bytes>*remaining||(limit&&(used>limit||bytes>limit-used)))throw Failure(413,"This IP has exhausted its upload allowance");state_.put("quota",ip,std::to_string(used+bytes));*remaining-=bytes;quota_->reserved[ip]-=bytes;quota_->used[ip]+=bytes;};
}
void Service::preflight_quota(const Request& request){
    if(request.prepaid)return;
    if(settings()->write_mode=="isolated"&&request.local_admin)return;
    if(request.header("content-length").empty())return;
    const auto length=parse_unsigned(request.header("content-length"),UINT64_MAX),limit=settings()->upload_limit(request.peer_ip);
    std::lock_guard<std::mutex> lock(quota_->mutex);const auto used=quota_->used.find(request.peer_ip),reservation=quota_->reserved.find(request.peer_ip);const auto amount=used==quota_->used.end()?0:used->second,reserved=reservation==quota_->reserved.end()?0:reservation->second;
    if(limit&&(amount>limit||reserved>limit-amount||length>limit-amount-reserved))throw Failure(413,"This IP has insufficient upload allowance remaining");
}
namespace {
namespace json=boost::json;using Clock=std::chrono::steady_clock;
bool inside(const std::string& root,const std::string& path){return path==root||(path.size()>root.size()&&path.compare(0,root.size(),root)==0&&path[root.size()]=='/');}
std::string field(const json::object& object,const char* key,std::size_t max=4096){const auto* value=object.if_contains(key);if(!value||!value->is_string()||value->as_string().size()>max)throw Failure(400,std::string("Invalid field: ")+key);return std::string(value->as_string());}
std::string param(const Request& request,const char* key,const std::string& fallback=""){const auto p=query_params(request.target);const auto it=p.find(key);return it==p.end()?fallback:it->second;}
json::object object(const std::string& body){boost::system::error_code ec;json::parse_options options;options.max_depth=8;const auto value=json::parse(body,ec,{},options);if(ec||!value.is_object())throw Failure(400,"Expected a JSON object");return value.as_object();}
Response encode(json::object value){Response result;result.body=json::serialize(value);result.headers={{"Cache-Control","no-store"}};return result;}
std::uint64_t number(const json::object& value,const char* key,std::uint64_t maximum=9007199254740991ull){const auto* item=value.if_contains(key);if(!item||(!item->is_int64()&&!item->is_uint64())||(item->is_int64()&&item->as_int64()<0))throw Failure(400,"Invalid numeric metadata");const auto n=item->is_uint64()?item->as_uint64():static_cast<std::uint64_t>(item->as_int64());if(n>maximum)throw Failure(400,"Metadata exceeds limit");return n;}
}
Response Service::remote_call(const std::string& mount,const std::string& job,const std::string& payload,std::function<Response(Response)> transform,std::shared_ptr<File> file){
    const auto id=random_token();Response result;
    result.cancel=[this,id]{std::lock_guard<std::mutex> lock(mount_mutex_);mount_jobs_.erase(id);};
    result.deferred=[this,mount,job,payload,id,transform,file](std::function<void(Response)> done){
        Response failure;bool ok=false;
        {std::lock_guard<std::mutex> lock(mount_mutex_);auto found=mounts_.find(mount);
            if(found==mounts_.end()||!found->second.remote||found->second.expires<Clock::now())failure=error(503,"Browser mount is offline; keep its provider page open");
            else{
                std::size_t count=0;for(const auto& item:mount_jobs_)if(item.second.mount==mount)++count;
                if(mount_jobs_.size()>=128||count>=32)failure=error(503,"Browser mount queue is full");
                else{
                    auto data=object(job);data["id"]=id;data["mount"]=found->second.id;
                    MountJob value;value.id=id;value.mount=mount;value.json=json::serialize(data);value.payload=payload;value.file=file;value.expires=Clock::now()+std::chrono::seconds(25);
                    if(data.at("op")=="materialize")value.expires=Clock::now()+std::chrono::seconds(45);
                    value.done=[done=std::move(done),transform](Response response)mutable{try{done(transform?transform(std::move(response)):std::move(response));}catch(const Failure& e){done(Service::error(e.status,e.what()));}catch(const std::exception&){done(Service::error(502,"Invalid browser mount response"));}};
                    mount_jobs_.emplace(id,std::move(value));ok=true;
                }
            }
        }
        if(!ok)done(std::move(failure));
    };
    return result;
}
void Service::maintain_mounts(){
    std::vector<std::function<void(Response)>> expired;
    {std::lock_guard<std::mutex> lock(mount_mutex_);const auto now=Clock::now();
        for(auto it=mount_jobs_.begin();it!=mount_jobs_.end();)if(it->second.expires<now){expired.push_back(std::move(it->second.done));it=mount_jobs_.erase(it);}else ++it;
        for(auto it=mount_uploads_.begin();it!=mount_uploads_.end();)if(it->second.expires<now)it=mount_uploads_.erase(it);else ++it;
    }
    for(auto& done:expired)done(error(504,"Browser mount response timed out"));
}
Response Service::mounts_api(const Request& request,const std::string& route){
    browser_guard(request,request.method!="GET");
    if(route=="/api/mounts"&&request.method=="GET"){Response r;r.body="{\"mounts\":"+mounts_json()+"}";r.headers={{"Cache-Control","no-store"}};return r;}
    if(route=="/api/mount/register"&&request.method=="POST"){
        const auto data=object(request.body);auto path=normalize_path(field(data,"path"));const auto browser=field(data,"browser",64);
        const auto* writable=data.if_contains("writable");if(!writable||!writable->is_bool()||browser.size()<16)throw Failure(400,"Invalid mount capabilities");
        const bool rw=writable->as_bool();if(rw&&settings()->write_mode=="readonly")throw Failure(403,"This share permits read-only mounts");
        if(path=="/"||path.rfind("/api",0)==0||path.rfind("/view",0)==0||path=="/download"||path=="/mcp"||path=="/preview"||path.rfind("/preview-assets",0)==0||path=="/_clients")throw Failure(403,"Mount destination is reserved");
        if(settings()->write_mode=="isolated"&&!request.local_admin&&!inside(owner_root(request),path))throw Failure(403,"Mount inside your own IP folder");
        if(settings()->write_mode=="readonly"&&!request.local_admin&&!inside("/_mounts/"+owner_root(request).substr(10),path))throw Failure(403,"Mount inside your own mount folder");
        std::unique_lock<std::mutex> write(write_mutex_);check_busy(path);
        try{resolve(path);throw Failure(409,"Mount destination already exists");}catch(const Failure& e){if(e.status!=404)throw;}
        {std::lock_guard<std::mutex> lock(mount_mutex_);std::size_t own=0;for(const auto& item:mounts_){if(inside(item.first,path)||inside(path,item.first))throw Failure(409,"Mounts cannot overlap");if(item.second.remote&&item.second.peer==request.peer_ip)++own;}if(mounts_.size()>=64||own>=8)throw Failure(409,"Mount capacity reached");}
        Mount mount;mount.path=path;mount.peer=request.peer_ip;mount.browser=browser;mount.remote=true;mount.writable=rw;mount.id=random_token();mount.key=random_token();mount.expires=Clock::now()+std::chrono::seconds(45);
        // Published replicas survive removing the provider mapping.
        mount.source=config_.root;
        const auto* movable=data.if_contains("movable");mount.movable=rw&&movable&&movable->is_bool()&&movable->as_bool();
        {std::lock_guard<std::mutex> lock(mount_mutex_);std::size_t own=0;
            for(const auto& item:mounts_){if(inside(item.first,path)||inside(path,item.first))throw Failure(409,"Mounts cannot overlap");if(item.second.remote&&item.second.peer==request.peer_ip)++own;}
            if(mounts_.size()>=64||own>=8)throw Failure(409,"At most 8 browser mounts per IP and 64 mounts globally");
            mounts_[path]=mount;
        }
        changed();return encode({{"path",path},{"id",mount.id},{"key",mount.key},{"writable",rw},{"movable",mount.movable}});
    }
    if(route=="/api/mount/native"&&request.method=="POST"){
        if(!request.local_admin)throw Failure(403,"Only the host can mount host paths");
        const auto data=object(request.body);const auto source=field(data,"source"),path=field(data,"path");
        const auto* rw=data.if_contains("writable");std::lock_guard<std::mutex> write(write_mutex_);const bool writable=rw&&rw->is_bool()&&rw->as_bool();add_native_mount(path,std::filesystem::u8path(source),writable);
        config_.mounts.push_back({normalize_path(path),canonical_root(std::filesystem::u8path(source)),writable});
        try{config_store_.save(config_,*settings());}catch(...){config_.mounts.pop_back();std::lock_guard<std::mutex> mounts(mount_mutex_);mounts_.erase(normalize_path(path));throw;}changed();return encode({{"ok",true}});
    }
    if(route=="/api/mount/mirror-directory"&&request.method=="POST"){
        const auto data=object(request.body);const auto path=normalize_path(field(data,"path"));Mount mount;if(!remote_mount(path,&mount)||mount.peer!=request.peer_ip||mount.key!=field(data,"key",128))throw Failure(403,"Invalid replica provider");
        protect_config(path);const auto storage=mapped(path);if(std::count(storage.path.begin(),storage.path.end(),'/')>64)throw Failure(400,"Directory depth exceeds bound");
        std::lock_guard<std::mutex> write(write_mutex_);
        if(settings()->write_mode=="isolated"&&!request.local_admin){try{resolve(owner_root(request));}catch(const Failure& e){if(e.status==404)return encode({{"ok",true}});throw;}}
        for(auto slash=storage.path.find('/',1);;slash=storage.path.find('/',slash+1)){const auto directory=slash==std::string::npos?storage.path:storage.path.substr(0,slash);secure_mkdir(storage.root,directory,true);if(slash==std::string::npos)break;}
        changed();return encode({{"ok",true}});
    }
    if(route=="/api/mount/remove"&&request.method=="POST"){
        const auto data=object(request.body);const auto path=normalize_path(field(data,"path"));
        std::unique_lock<std::mutex> write(write_mutex_);
        std::vector<std::function<void(Response)>> waiting;
        {std::lock_guard<std::mutex> lock(mount_mutex_);const auto found=mounts_.find(path);if(found==mounts_.end())throw Failure(404,"Mount not found");if(!found->second.remote)check_busy(path);
            if(!request.local_admin&&(!found->second.remote||found->second.peer!=request.peer_ip||field(data,"key",128)!=found->second.key))throw Failure(403,"Only the provider or host can unmount");
            for(auto it=mount_jobs_.begin();it!=mount_jobs_.end();)if(it->second.mount==path){waiting.push_back(std::move(it->second.done));it=mount_jobs_.erase(it);}else ++it;
            for(auto it=mount_uploads_.begin();it!=mount_uploads_.end();)if(it->second.mount==path)it=mount_uploads_.erase(it);else ++it;
            if(!found->second.remote){
                auto previous=config_.mounts;config_.mounts.erase(std::remove_if(config_.mounts.begin(),config_.mounts.end(),[&](const auto& m){return normalize_path(m.path)==path;}),config_.mounts.end());
                try{config_store_.save(config_,*settings());}catch(...){config_.mounts=std::move(previous);throw;}
            }
            mounts_.erase(found);
        }
        write.unlock();for(auto& done:waiting)done(error(503,"Mount was removed"));changed();return encode({{"ok",true}});
    }
    if(route=="/api/mount/poll"&&request.method=="POST"){
        const auto data=object(request.body);const auto* providers=data.if_contains("providers");if(!providers||!providers->is_array()||providers->as_array().size()>8)throw Failure(400,"Invalid provider list");
        json::array jobs,removed;std::lock_guard<std::mutex> lock(mount_mutex_);
        for(const auto& item:providers->as_array()){
            if(!item.is_object())throw Failure(400,"Invalid provider");
            const auto id=field(item.as_object(),"id",128),key=field(item.as_object(),"key",128);
            bool found=false;
            for(auto& m:mounts_)if(m.second.id==id&&m.second.remote){
                found=true;
                if(m.second.peer!=request.peer_ip||m.second.key!=key)throw Failure(403,"Invalid provider identity");
                m.second.expires=Clock::now()+std::chrono::seconds(45);
                for(auto& job:mount_jobs_)if(job.second.mount==m.first){
                    const auto info=json::parse(job.second.json).as_object();if(info.at("op")=="materialize"||info.at("op")=="write-cache")job.second.expires=Clock::now()+std::chrono::seconds(45);
                    if(!job.second.sent&&jobs.size()<4){job.second.sent=true;jobs.push_back(info);}
                }
            }
            if(!found)removed.emplace_back(id);
        }
        return encode({{"jobs",std::move(jobs)},{"removed",std::move(removed)}});
    }
    if((route=="/api/mount/reply"&&request.method=="POST")||(route=="/api/mount/payload"&&request.method=="GET")){
        const auto id=param(request,"id"),key=param(request,"key"),mid=param(request,"mount");
        std::function<void(Response)> done;Response value;
        {std::lock_guard<std::mutex> lock(mount_mutex_);const auto job=mount_jobs_.find(id);if(job==mount_jobs_.end())throw Failure(410,"Mount request expired");
            const auto provider=mounts_.find(job->second.mount);if(provider==mounts_.end()||provider->second.id!=mid||provider->second.key!=key||provider->second.peer!=request.peer_ip)throw Failure(403,"Invalid provider identity");
            if(route=="/api/mount/payload"){Response response;response.content_type="application/octet-stream";response.headers.emplace_back("Cache-Control","no-store");if(job->second.file){auto file=job->second.file;response.file=file;response.length=file->info().size;auto range=parse_range(request.header("range"),response.length);if(range.state!=RangeState::valid)throw Failure(416,"Expected bounded provider payload range");if(range.length>262144)throw Failure(413,"Provider payload range exceeds 256 KiB");response.offset=range.first;response.length=range.length;response.status=206;response.headers.emplace_back("Content-Range","bytes "+std::to_string(range.first)+"-"+std::to_string(range.first+range.length-1)+"/"+std::to_string(file->info().size));return response;}response.body=job->second.payload;return response;}
            value.body=request.body;const auto status=param(request,"status","200");value.status=static_cast<unsigned>(parse_unsigned(status,599));if(value.status<200)throw Failure(400,"Invalid provider status");
            done=std::move(job->second.done);mount_jobs_.erase(job);
        }
        done(std::move(value));return encode({{"ok",true}});
    }
    // Uploads use bounded chunks and backpressure; no complete-file proxy buffer.
    if(route.rfind("/api/mount/upload-",0)==0&&request.method=="POST"){
        const auto logical=normalize_path(param(request,"path"));authorize(request,logical);Mount mount;if(!remote_mount(logical,&mount))throw Failure(404,"Browser mount not found");
        const auto relative=logical.substr(mount.path.size());
        if(route=="/api/mount/upload-begin"){
            const auto data=object(request.body);const auto size=number(data,"size",settings()->max_upload_bytes);const auto version=field(data,"version",512);const auto id=random_token();
            {std::lock_guard<std::mutex> lock(mount_mutex_);if(mount_uploads_.size()>=32)throw Failure(503,"Too many browser uploads");for(const auto& item:mount_uploads_)if(item.second.path==logical)throw Failure(409,"Another upload uses this path");mount_uploads_[id]={id,mount.path,logical,request.peer_ip,version,size,0,true,Clock::now()+std::chrono::seconds(90)};}
            return remote_call(mount.path,json::serialize(json::object{{"op","write-begin"},{"path",relative},{"upload",id},{"version",version}}),"",[this,id](Response r){
                if(r.status>=400){std::lock_guard<std::mutex> lock(mount_mutex_);mount_uploads_.erase(id);return r;}
                {std::lock_guard<std::mutex> lock(mount_mutex_);auto it=mount_uploads_.find(id);if(it==mount_uploads_.end())return error(410,"Upload expired");it->second.busy=false;}
                return encode({{"upload",id}});
            });
        }
        const auto id=param(request,"upload");MountUpload state;
        {std::lock_guard<std::mutex> lock(mount_mutex_);auto it=mount_uploads_.find(id);if(it==mount_uploads_.end()||it->second.peer!=request.peer_ip||it->second.path!=logical)throw Failure(403,"Invalid browser upload");
            if(it->second.busy)throw Failure(409,"Upload chunk still active");
            it->second.busy=true;it->second.expires=Clock::now()+std::chrono::seconds(90);state=it->second;
        }
        if(route=="/api/mount/upload-chunk"){
            if(request.body.size()>262144||request.body.size()>state.length-state.received){std::lock_guard<std::mutex> lock(mount_mutex_);mount_uploads_.erase(id);throw Failure(413,"Upload chunk exceeds bound");}
            const auto bytes=request.body.size();try{if(settings()->write_mode!="isolated"||!request.local_admin)charge_upload(request.peer_ip,bytes);}catch(...){std::lock_guard<std::mutex> lock(mount_mutex_);mount_uploads_.erase(id);throw;}return remote_call(mount.path,json::serialize(json::object{{"op","write-chunk"},{"upload",id},{"offset",state.received},{"bytes",bytes}}),request.body,[this,id,bytes](Response r){std::lock_guard<std::mutex> lock(mount_mutex_);const auto it=mount_uploads_.find(id);if(it!=mount_uploads_.end()){it->second.busy=false;if(r.status<400)it->second.received+=bytes;else mount_uploads_.erase(it);}return r;});
        }
        if(route=="/api/mount/upload-end"||route=="/api/mount/upload-abort"){
            const bool abort=route=="/api/mount/upload-abort";if(!abort&&state.received!=state.length){std::lock_guard<std::mutex> lock(mount_mutex_);mount_uploads_.erase(id);throw Failure(400,"Upload is incomplete");}
            return remote_call(mount.path,json::serialize(json::object{{"op",abort?"write-abort":"write-end"},{"upload",id}}),"",[this,id,logical,abort](Response r){
                {std::lock_guard<std::mutex> lock(mount_mutex_);mount_uploads_.erase(id);}if(r.status<400&&!abort){changed();auto value=object(r.body);value["path"]=logical;value["type"]=json::parse(type_json(extension_type(logical),std::filesystem::u8path(logical).filename().u8string()));r.body=json::serialize(value);r.status=201;}return r;
            });
        }
    }
    throw Failure(404,"Unknown mount endpoint");
}
Response Service::mount_dispatch(const Request& request,const std::string& route,const std::string& logical){
    protect_config(logical);if(request.method=="GET"||request.method=="HEAD")script_file(request,"read",logical);else script_file(request,"provider-write",logical);
    Mount mount;if(!remote_mount(logical,&mount))throw Failure(404,"Mount not found");
    const auto pinned=param(request,"version");
    if(route=="/download"&&pinned.rfind("mount:",0)==0){
        P2PState::Peer peer;peer.path=logical;peer.version=pinned;peer.source_mount=mount.id;peer.source_version=pinned.substr(6);
        auto mirror=[this,request,logical,mount,pinned](Response response){
            if(response.status>=400)return response;
            Mount current;if(!remote_mount(logical,&current)||current.id!=mount.id)throw Failure(409,"Mounted source changed");
            const auto copy=current.mirrored.find(logical);const auto file=open_file(logical);
            if(copy==current.mirrored.end()||copy->second.source!=pinned.substr(6)||copy->second.host!=file->info().version)throw Failure(409,"Mounted replica changed");
            auto next=request;next.mirror_local=true;next.target="/download?path="+url_encode(logical)+"&version="+url_encode(file->info().version);return handle(next);
        };
        const auto copy=mount.mirrored.find(logical);
        if(copy!=mount.mirrored.end()&&copy->second.source==peer.source_version){try{const auto file=open_file(logical);if(file->info().version==copy->second.host)return mirror(Response{});}catch(const Failure& e){if(e.status!=404)throw;}}
        return remote_call(mount.path,json::serialize(json::object{{"op","materialize"},{"path",logical.substr(mount.path.size())},{"version",peer.source_version}}),"",std::move(mirror));
    }
    // Version-pinned bytes already on the host bypass the provider entirely.
    // The ordinary File open still validates the host generation and Range.
    if((request.method=="GET"||request.method=="HEAD")&&(route=="/download"||route.rfind("/view/",0)==0)&&!param(request,"version").empty()){
        try{open_file(logical);auto next=request;next.mirror_local=true;return handle(next);}catch(const Failure& e){if(e.status!=404)throw;}
    }
    if(mount.expires<Clock::now()&&(request.method=="GET"||request.method=="HEAD")){auto next=request;next.mirror_local=true;return handle(next);}
    const auto relative=logical==mount.path?"/":logical.substr(mount.path.size());
    const bool read=request.method=="GET"||request.method=="HEAD";
    if(!read){
        if(mount.expires<Clock::now())throw Failure(503,"Browser mount is offline");
        authorize(request,logical);const auto destination=normalize_path(param(request,"to","/"));std::string op;
        if(route=="/api/mkdir"&&request.method=="POST")op="mkdir";
        else if(route=="/api/entry"&&request.method=="DELETE")op="delete";
        else if(route=="/api/rename"&&request.method=="POST"){
            authorize(request,destination);if(!mount.movable)throw Failure(501,"This browser cannot move mounted entries");if(!inside(mount.path,destination))throw Failure(409,"Cannot move across mounts");op="move";
        }else throw Failure(405,"Unsupported browser mount operation");
        protect_config(logical,true);if(op=="move")protect_config(destination,true);
        {std::lock_guard<std::mutex> lock(mount_mutex_);for(const auto& item:mount_uploads_)if(inside(logical,item.second.path)||inside(item.second.path,logical)||(op=="move"&&inside(destination,item.second.path)))throw Failure(409,"An upload uses this path");}
        return remote_call(mount.path,json::serialize(json::object{{"op",op},{"path",relative},{"to",op=="move"?destination.substr(mount.path.size()):""},{"recursive",param(request,"recursive")=="1"},{"empty",param(request,"empty")=="1"},{"noReplace",true},{"version",param(request,"version")}}),"",[this,logical,destination,op](Response r){
            if(r.status<400){std::lock_guard<std::mutex> write(write_mutex_);try{if(op=="delete")remove_path(logical,true);else if(op=="move")rename_path(logical,destination);else mkdir_path(logical,true);}catch(const Failure& e){if(e.status!=404)throw;}
                changed();
            }return r;
        });
    }
    if(route=="/api/list"||route=="/api/search"){
        note_browse(request,logical,param(request,"browser"));const auto offset=parse_unsigned(param(request,"offset","0"),config_.directory_limit),limit=parse_unsigned(param(request,"limit","100"),500);if(!limit)throw Failure(400,"Page limit must be positive");
        return remote_call(mount.path,json::serialize(json::object{{"op","list"},{"path",relative},{"offset",offset},{"limit",limit},{"sort",param(request,"sort","name")},{"order",param(request,"order","asc")},{"showHidden",param(request,"showHidden")=="1"},{"q",route=="/api/search"?param(request,"q"):""},{"focus",param(request,"focus")}}),"",[this,mount,logical,offset,limit](Response r){
            if(r.status>=400)return r;
            auto data=object(r.body);auto* entries=data.if_contains("entries");if(!entries||!entries->is_array()||entries->as_array().size()>limit)throw Failure(502,"Invalid mounted listing");number(data,"total",config_.directory_limit);
            const auto reported_offset=number(data,"offset",config_.directory_limit);
            for(auto& item:entries->as_array()){
                if(!item.is_object())throw Failure(502,"Invalid mounted entry");
                auto& entry=item.as_object();const auto name=field(entry,"name",255);if(name.empty()||name.find('/')!=std::string::npos||name=="."||name=="..")throw Failure(502,"Unsafe mounted filename");
                const auto* directory=entry.if_contains("directory");if(!directory||!directory->is_bool())throw Failure(502,"Invalid mounted entry type");number(entry,"size");number(entry,"modified");
                const auto relative=entry.if_contains("relative");const auto path=relative?normalize_path(mount.path+field(entry,"relative")):normalize_path(logical+"/"+name);
                if(!inside(logical,path)||std::filesystem::u8path(path).filename().u8string()!=name)throw Failure(502,"Mounted search result escapes its scope");
                entry["path"]=path;entry["type"]=json::parse(type_json(extension_type(name),name));entry["mounted"]=true;entry["writable"]=mount.writable;
            }
            if(!data.if_contains("focusIndex")&&reported_offset!=offset)throw Failure(502,"Mounted listing offset mismatch");
            data["indexing"]=false;data["mounts"]=json::parse(mounts_json());r.body=json::serialize(data);return decorate_entries(std::move(r));
        });
    }
    const bool preview=route.rfind("/view/",0)==0||route=="/api/preview-text"||(route.rfind("/api/",0)!=0&&route!="/download");
    const bool hosting=config_.mode=="server"&&route.rfind("/api/",0)!=0&&route.rfind("/view/",0)!=0&&route!="/download";
    if(preview&&!hosting&&!settings()->preview)throw Failure(403,"Preview is disabled");
    return remote_call(mount.path,json::serialize(json::object{{"op","stat"},{"path",relative}}),"",[this,mount,request,route,logical,relative,preview](Response r){
        if(r.status>=400){if(route=="/api/upload-target"&&r.status==404)return encode({{"exists",false}});return r;}
        const auto data=object(r.body);const auto* dir=data.if_contains("directory");if(!dir||!dir->is_bool())throw Failure(502,"Invalid mounted file metadata");
        if(route=="/api/upload-target"){auto info=data;info["exists"]=true;return encode(std::move(info));}
        if(dir->as_bool()){
            if(!preview)throw Failure(403,"Only ordinary files may be downloaded");
            auto next=request;const auto raw=request.target.substr(0,request.target.find('?'));if(raw.back()!='/'){Response response;response.status=302;response.headers.emplace_back("Location",raw+"/");return response;}
            return mount_dispatch(next,route,logical+"/index.html");
        }
        const auto length=number(data,"size");const auto modified=number(data,"modified");const auto version=field(data,"version",512);
        if(route=="/api/download-ticket"&&param(request,"p2p")=="1"&&settings()->p2p){
            return encode({{"url","/download?path="+url_encode(logical)+"&version="+url_encode("mount:"+version)},{"size",length},{"modified",modified},{"version","mount:"+version}});
        }
        bool ready=false;{std::lock_guard<std::mutex> lock(mount_mutex_);const auto current=mounts_.find(mount.path);if(current!=mounts_.end()){const auto found=current->second.mirrored.find(logical);ready=found!=current->second.mirrored.end()&&found->second.source==version;}}
        if(ready){try{const auto file=open_file(logical);const auto replica=mount.mirrored.find(logical);ready=replica!=mount.mirrored.end()&&file->info().version==replica->second.host;}catch(const Failure& e){if(e.status!=404)throw;ready=false;}}
        auto deliver=[this,request,route,version](){auto next=request;next.mirror_local=true;auto response=handle(next);if(route=="/api/download-ticket"&&response.status<400){auto ticket=object(response.body);ticket["sourceVersion"]=version;response.body=json::serialize(ticket);}return response;};
        if(ready)return deliver();
        return remote_call(mount.path,json::serialize(json::object{{"op","materialize"},{"path",relative},{"version",version}}),"",[deliver](Response result){if(result.status>=400)return result;return deliver();});
    });
}
}
