// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include "sha256.hpp"
#include <boost/json.hpp>
#include <algorithm>
#include <ctime>

namespace hls {
namespace {
namespace j = boost::json;
std::uint64_t stamp(){return static_cast<std::uint64_t>(std::time(nullptr));}
std::string text(const j::object& o,const char* key){auto* v=o.if_contains(key);if(!v)return {};if(!v->is_string())throw Failure(400,"Expected string");return std::string(v->as_string());}
std::uint64_t number(const j::object& o,const char* key){auto* v=o.if_contains(key);if(!v)return 0;if(v->is_uint64())return v->as_uint64();if(v->is_int64()&&v->as_int64()>=0)return static_cast<std::uint64_t>(v->as_int64());throw Failure(400,"Expected unsigned number");}
std::string parameter(const Request& r,const char* name){auto q=query_params(r.target);auto it=q.find(name);return it==q.end()?std::string():it->second;}
j::object object(const std::string& bytes){boost::system::error_code ec;j::parse_options options;options.max_depth=16;auto v=j::parse(bytes,ec,{},options);if(ec||!v.is_object())throw Failure(400,"Expected bounded JSON object");return v.as_object();}
bool capability(const std::string& v){return v.size()==48&&std::all_of(v.begin(),v.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
Response encoded(j::object value,unsigned status=200){Response r;r.status=status;r.body=j::serialize(value);r.headers.emplace_back("Cache-Control","no-store");return r;}
}
void Service::maintain_transfer_cache(){
    std::unique_lock<std::mutex> lock(transfer_cache_mutex_,std::try_to_lock);
    if(!lock.owns_lock())return;
    unsigned count=0;
    for(const auto& item:state_.scan("transfer-cache")){
        if(++count>128)break;
        try{auto v=object(item.second);if(number(v,"expires")>=stamp()||resume_lock("cache-"+item.first)->busy.load())continue;secure_remove(state_.root(),"/cache-"+item.first,false,1);state_.erase("transfer-cache",item.first);state_.erase("transfer-journal",item.first);}catch(const Failure& e){if(e.status==404){state_.erase("transfer-cache",item.first);state_.erase("transfer-journal",item.first);}}
    }
}
Response Service::transfer_cache_api(const Request& request,const std::string& route){
    browser_guard(request,request.method!="GET"&&request.method!="HEAD");
    if(!access_guard(request,true))throw Failure(401,"Login required");
    std::lock_guard<std::mutex> lock(transfer_cache_mutex_);
    const auto identity=state_.get("browser-namespace",config_.root.u8string());
    if(route=="/api/transfer-cache/create"&&request.method=="POST"){
        auto input=object(request.body);const auto path=normalize_path(text(input,"path"));protect_config(path);script_file(request,"read",path);
        const auto purpose=text(input,"purpose");if(!purpose.empty()&&purpose!="upload")throw Failure(400,"Invalid cache purpose");if(purpose=="upload"){authorize(request,path);script_file(request,"upload",path,number(input,"size"));Mount mount;if(!remote_mount(path,&mount)||!mount.writable)throw Failure(403,"Expected writable browser mount");if(number(input,"size")>settings()->max_upload_bytes)throw Failure(413,"Upload exceeds configured limit");}
        const auto records=state_.scan("transfer-cache");unsigned own=0;for(const auto& r:records)if(text(object(r.second),"ip")==request.peer_ip)++own;
        if(records.size()>=128||own>=8)throw Failure(507,"Private transfer cache session limit reached");
        auto name=text(input,"name");if(name.empty())name="download.zip";if(name.size()>1024||name.find_first_of("\\/\r\n")!=std::string::npos)throw Failure(400,"Invalid transfer name");
        const auto id=random_token(),key=random_token();secure_spool(state_.root(),"/cache-"+id,0,"");
        j::object value{{"key",key},{"ip",request.peer_ip},{"namespace",identity},{"path",path},{"purpose",purpose},{"size",number(input,"size")},{"chain",""},{"name",name},{"offset",0},{"checkpoint",0},{"complete",false},{"expires",stamp()+config_.resume_hours*3600ull}};
        state_.put("transfer-cache",id,j::serialize(value));return encoded({{"id",id},{"key",key},{"offset",0}},201);
    }
    const auto id=parameter(request,"id"),key=parameter(request,"key");if(!capability(id)||!capability(key))throw Failure(400,"Invalid transfer capability");
    auto stored=state_.get("transfer-cache",id);if(stored.empty())throw Failure(404,"Private transfer cache expired");auto v=object(stored);
    if(text(v,"key")!=key||text(v,"ip")!=request.peer_ip||text(v,"namespace")!=identity)throw Failure(403,"Private transfer belongs to another client or share");
    if(number(v,"expires")<stamp())throw Failure(410,"Private transfer cache expired");
    protect_config(text(v,"path"));script_file(request,"read",text(v,"path"));
    const auto offset=number(v,"offset"),checkpoint=number(v,"checkpoint");const auto storage="/cache-"+id;
    auto save=[&]{v["expires"]=stamp()+config_.resume_hours*3600ull;state_.put("transfer-cache",id,j::serialize(v));};
    if(route=="/api/transfer-cache/status"&&request.method=="GET"){
        auto journal=state_.get("transfer-journal",id);save();j::object status{{"offset",offset},{"checkpoint",checkpoint},{"chain",text(v,"chain")},{"journal",journal.empty()?j::value(nullptr):j::parse(journal)},{"complete",v.at("complete")}};if(auto* receipt=v.if_contains("receipt"))status["receipt"]=*receipt;return encoded(std::move(status));
    }
    if(route=="/api/transfer-cache/read"&&(request.method=="GET"||request.method=="HEAD")){
        auto file=File::open(state_.root(),storage);Response r;r.file=file;r.length=offset;r.content_type="application/octet-stream";r.headers={{"Accept-Ranges","bytes"},{"Cache-Control","no-store"},{"Content-Disposition","attachment; filename=\"download\"; filename*=UTF-8''"+url_encode(text(v,"name"))}};
        auto range=parse_range(request.header("range"),offset);if(range.state==RangeState::unsatisfiable){auto e=error(416,"Private transfer range exceeds stored bytes");e.headers.emplace_back("Content-Range","bytes */"+std::to_string(offset));return e;}if(range.state==RangeState::valid){r.status=206;r.offset=range.first;r.length=range.length;r.headers.emplace_back("Content-Range","bytes "+std::to_string(range.first)+"-"+std::to_string(range.first+range.length-1)+"/"+std::to_string(offset));}save();return r;
    }
    if(request.method!="POST")throw Failure(405,"Unsupported private transfer method");
    auto activity=resume_lock("cache-"+id);if(activity->busy.load())throw Failure(409,"Transfer publication still active");
    if(route=="/api/transfer-cache/cancel"){secure_remove(state_.root(),storage,false,1);state_.erase("transfer-cache",id);state_.erase("transfer-journal",id);return encoded({{"ok",true}});}
    if(route=="/api/transfer-cache/publish-mount"){
        if(text(v,"purpose")!="upload"||offset!=number(v,"size"))throw Failure(409,"Mounted upload is incomplete");
        if(auto* receipt=v.if_contains("receipt"))return encoded(receipt->as_object());
        const auto input=object(request.body);const auto path=text(v,"path");if(text(input,"version").size()>512)throw Failure(400,"Provider version exceeds bound");authorize(request,path);script_file(request,"provider-write",path,offset);Mount mount;if(!remote_mount(path,&mount)||!mount.writable)throw Failure(403,"Browser mount no longer writable");
        auto file=File::open(state_.root(),storage);if(file->info().size!=offset)throw Failure(409,"Upload spool size changed");
        activity->busy=true;auto lease=std::shared_ptr<void>(reinterpret_cast<void*>(1),[activity](void*){activity->busy=false;});
        return remote_call(mount.path,j::serialize(j::object{{"op","write-cache"},{"path",path.substr(mount.path.size())},{"size",offset},{"version",text(input,"version")}}),"",[this,id,key,path,offset,lease,request,file](Response response){
            if(response.status>=400)return response;const auto result=object(response.body);const auto version=text(result,"version");if(version.empty()||version.size()>512||number(result,"size")!=offset)throw Failure(502,"Invalid provider publication receipt");
            return publish_mount_cache(request,id,key,path,version,file,lease);
        },file);
    }
    if(v.at("complete").as_bool())throw Failure(409,"Private transfer is already complete");
    if(route=="/api/transfer-cache/chunk"||route=="/api/transfer-cache/copy-chunk"){
        std::string payload=request.body;std::uint64_t at=0;
        if(route=="/api/transfer-cache/copy-chunk"){auto input=object(request.body);at=number(input,"offset");const auto source=normalize_path(text(input,"source"));protect_config(source);script_file(request,"read",source);auto file=open_file(source);if(text(input,"version")!=file->info().version||at>=file->info().size)throw Failure(409,"Copy source changed or offset invalid");payload.resize(static_cast<std::size_t>(std::min<std::uint64_t>(1048576,file->info().size-at)));if(file->read(at,payload.data(),payload.size())!=payload.size()||!file->unchanged())throw Failure(409,"Copy source changed during read");}
        else at=parse_unsigned(parameter(request,"offset"),UINT64_MAX);
        if(payload.empty()||payload.size()>1048576||at>offset)throw Failure(400,"Invalid private transfer chunk");
        if(text(v,"purpose")=="upload"){authorize(request,text(v,"path"));if(offset>number(v,"size")||payload.size()>number(v,"size")-at||number(v,"size")>settings()->max_upload_bytes)throw Failure(413,"Upload exceeds declared or configured limit");if(at%1048576||payload.size()!=std::min<std::uint64_t>(1048576,number(v,"size")-at))throw Failure(400,"Expected 1 MiB upload boundaries");}
        if(at<offset){auto old=File::open(state_.root(),storage);std::string bytes(payload.size(),'\0');if(payload.size()>offset-at||old->read(at,bytes.data(),bytes.size())!=bytes.size()||bytes!=payload)throw Failure(409,"Private transfer retry differs");return encoded({{"offset",offset}});}
        std::uint64_t used=0;for(const auto& row:state_.scan("transfer-cache")){const auto size=number(object(row.second),"offset");if(used>config_.resume_bytes||size>config_.resume_bytes-used)throw Failure(507,"Private transfer cache budget reached");used+=size;}
        {std::lock_guard<std::mutex> resume(resume_mutex_);for(const auto& row:resume_summary_)if(!row.second.done){if(used>config_.resume_bytes||row.second.size>config_.resume_bytes-used)throw Failure(507,"Transfer storage budget reached");used+=row.second.size;}}
        if(payload.size()>config_.resume_bytes-used)throw Failure(507,"Transfer storage budget reached");
        if(text(v,"purpose")=="upload"&&(settings()->write_mode!="isolated"||!request.local_admin))charge_upload(request.peer_ip,payload.size());
        secure_spool(state_.root(),storage,offset,payload);if(text(v,"purpose")=="upload"){Sha256 piece;piece.update(payload.data(),payload.size());const auto material=text(v,"chain")+piece.finish();Sha256 chain;chain.update(material.data(),material.size());v["chain"]=chain.finish();}v["offset"]=offset+payload.size();save();return encoded({{"offset",offset+payload.size()}});
    }
    if(route=="/api/transfer-cache/checkpoint"){
        if(request.body.size()>4u*1024u*1024u)throw Failure(413,"Transfer journal exceeds bound");auto input=object(request.body);const auto at=number(input,"offset");if(at>offset)throw Failure(409,"Checkpoint exceeds stored bytes");
        const auto* journal=input.if_contains("journal");if(!journal||!journal->is_object())throw Failure(400,"Expected transfer journal");
        v["checkpoint"]=at;v["expires"]=stamp()+config_.resume_hours*3600ull;auto merged=journal->as_object();if(!merged.if_contains("plan")){const auto previous=state_.get("transfer-journal",id);if(!previous.empty()){auto old=object(previous);if(auto* plan=old.if_contains("plan"))merged["plan"]=*plan;}}state_.transfer_checkpoint(id,j::serialize(v),j::serialize(merged));return encoded({{"offset",at}});
    }
    if(route=="/api/transfer-cache/trim"){
        const auto at=number(object(request.body),"offset");if(at>checkpoint)throw Failure(409,"Cannot retain uncommitted transfer bytes");secure_spool(state_.root(),storage,at,"");v["offset"]=at;save();return encoded({{"offset",at}});
    }
    if(route=="/api/transfer-cache/complete"){v["complete"]=true;v["checkpoint"]=offset;save();return encoded({{"url","/api/transfer-cache/read?id="+id+"&key="+key},{"size",offset}});}
    throw Failure(404,"Unknown private transfer operation");
}
// Copy the bytes already on the host instead of making the provider upload again.
Response Service::publish_mount_cache(const Request& request,const std::string& id,const std::string& key,const std::string& path,const std::string& version,std::shared_ptr<File> spool,std::shared_ptr<void> lease){
    Mount mount;std::shared_ptr<Upload> upload;
    {std::lock_guard<std::mutex> write(write_mutex_);authorize(request,path);check_busy(path);protect_config(path);if(!remote_mount(path,&mount)||!mount.writable)throw Failure(409,"Mount changed during publication");
        const auto storage=mapped(path);if(settings()->write_mode=="isolated"&&!request.local_admin)ensure_owner(request);
        for(auto slash=storage.path.find('/',1);slash!=std::string::npos;slash=storage.path.find('/',slash+1))secure_mkdir(storage.root,storage.path.substr(0,slash),true);
        std::string previous;try{previous=File::open(storage.root,storage.path)->info().version;}catch(const Failure& e){if(e.status!=404)throw;}
        upload=std::make_shared<Upload>(storage.root,storage.path,settings()->max_upload_bytes,previous,path);uploads_[path]=upload;
    }
    struct Copy{Request request;Mount mount;std::string id,key,path,version;std::shared_ptr<File> spool;std::shared_ptr<Upload> upload;std::shared_ptr<void> lease;std::atomic<bool> cancelled{false};std::uint64_t at=0;};
    auto job=std::make_shared<Copy>();job->request=request;job->mount=mount;job->id=id;job->key=key;job->path=path;job->version=version;job->spool=std::move(spool);job->upload=std::move(upload);job->lease=std::move(lease);
    Response response;response.cancel=[job]{job->cancelled=true;};response.deferred=[this,job](std::function<void(Response)> done){
        auto step=std::make_shared<std::function<void()>>();std::weak_ptr<std::function<void()>> weak=step;*step=[this,job,done,weak]{auto next=weak.lock();if(!next)return;if(!copy_workers_.submit([this,job,done,next]{try{
            if(job->cancelled)throw Failure(499,"Mount publication paused; retry to continue");Mount current;if(!remote_mount(job->path,&current)||current.id!=job->mount.id)throw Failure(409,"Mount removed during publication");
            auto block=cache_.transfer_buffer();std::size_t budget=4u<<20;while(job->at<job->spool->info().size&&budget){const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(std::min(block->size,budget),job->spool->info().size-job->at));if(job->spool->read(job->at,block->data,n)!=n)throw Failure(500,"Incomplete cached payload");job->upload->append(block->data,n);job->at+=n;budget-=n;}
            if(job->at<job->spool->info().size){(*next)();return;}job->upload->prepare();
            {std::lock_guard<std::mutex> write(write_mutex_);authorize(job->request,job->path);protect_config(job->path);if(!remote_mount(job->path,&current)||current.id!=job->mount.id)throw Failure(409,"Mount removed before publication");
                std::lock_guard<std::mutex> mounts(mount_mutex_);auto& mounted=mounts_.at(current.path);const auto old=mounted.mirrored.find(job->path);const auto oldBytes=old==mounted.mirrored.end()?0:old->first.size()+old->second.source.size()+old->second.host.size()+128;const auto addition=job->path.size()+job->version.size()+512;std::size_t total=0;for(const auto& item:mounts_)total+=item.second.metadata_bytes;
                if(mounted.metadata_bytes-oldBytes+addition>(8ull<<20)||total-oldBytes+addition>(32ull<<20))throw Failure(507,"Mount metadata budget reached");
                job->upload->commit();uploads_.erase(job->path);const auto host=File::open(config_.root,job->path)->info().version;mounted.mirrored[job->path]={job->version,host};mounted.metadata_bytes=mounted.metadata_bytes-oldBytes+job->path.size()+job->version.size()+host.size()+128;
            }changed();
            if(settings()->write_mode=="isolated"&&!job->request.local_admin){std::lock_guard<std::mutex> aliases(alias_mutex_);const auto own=owner_root(job->request);if(aliases_.count(own)||aliases_.size()<4096){aliases_[own].uploaded=true;persist_alias(own,&aliases_[own]);}}
            const auto name=std::filesystem::u8path(job->path).filename().u8string();const auto committed=File::open(config_.root,job->path);
            j::object receipt{{"version",job->version},{"size",job->at},{"path",job->path},{"type",j::parse(type_json(identify(committed,name),name))}};{std::lock_guard<std::mutex> lock(transfer_cache_mutex_);auto metadata=object(state_.get("transfer-cache",job->id));if(text(metadata,"key")!=job->key||text(metadata,"ip")!=job->request.peer_ip)throw Failure(403,"Upload identity changed");metadata["receipt"]=receipt;metadata["complete"]=true;state_.put("transfer-cache",job->id,j::serialize(metadata));}job->lease.reset();done(encoded(std::move(receipt),201));
        }catch(const Failure& e){job->upload.reset();job->lease.reset();done(error(e.status,e.what()));}catch(const std::exception&){job->upload.reset();job->lease.reset();done(error(500,"Cached publication failed; source bytes retained"));}})){job->upload.reset();job->lease.reset();done(error(503,"Storage queue full; retry publication"));}};(*step)();
    };return response;
}

}
