#include "version.hpp"
#include "service.hpp"
#include "util.hpp"
#include "web_assets.hpp"
#include "file_type.hpp"
#include "network_features.hpp"
#include <algorithm>
#include <ctime>
#include <sstream>

namespace hls {
namespace {
std::string parameter(const std::map<std::string,std::string>& p,const std::string& name,const std::string& fallback="") { const auto it=p.find(name); return it==p.end()?fallback:it->second; }
std::size_t page_number(const std::map<std::string,std::string>& p,const std::string& name,std::size_t fallback,std::size_t maximum) { auto it=p.find(name); return it==p.end()?fallback:static_cast<std::size_t>(parse_unsigned(it->second,maximum)); }
Response entries_response(const std::vector<DirectoryEntry>& entries,std::size_t total,std::size_t offset,bool truncated,bool indexing=false) {
    Response response;
    response.body="{\"total\":"+std::to_string(total)+",\"offset\":"+std::to_string(offset)+",\"truncated\":"+(truncated?"true":"false")+",\"indexing\":"+(indexing?"true":"false")+",\"entries\":[";
    bool comma=false; for(const auto& entry:entries) { if(comma) response.body+=','; comma=true; response.body+=entry_json(entry); }
    response.body+="]}"; response.headers.emplace_back("Cache-Control","no-store"); return response;
}
}
Service::Service(const Config& config, WorkerPool& workers, RuntimeStats& runtime) : copy_workers_(workers),config_(config),config_store_(config),state_(config),scripts_(config), runtime_(runtime), cache_(config,workers),index_(config,config_store_),inference_(config.magika_threads,config.cpu_kernel) {
    config_store_.depend(state_.root(),true);for(const auto* name:{"state.sqlite","state.sqlite-wal","state.sqlite-shm","owner.lock"})config_store_.depend(state_.root()/name);if(!scripts_.path().empty())config_store_.depend(scripts_.path());if(config_.https){config_store_.depend(config_.tls_key);config_store_.depend(config_.tls_cert);}
    live_=std::make_shared<const LiveSettings>(config);init_mounts();restore_durable();init_zip();scripts_.mutation_mutex(&write_mutex_,&config_store_,&state_);if(state_.get("browser-namespace",config_.root.u8string()).empty())state_.put("browser-namespace",config_.root.u8string(),random_token());if(config_.config_explicit&&config_store_.enabled()&&!config_store_.exists())config_store_.save(config_,*settings());}
void Service::start() { index_.start(); }
void Service::stop() { zip_workers_.stop(); index_.stop(); }
void Service::maintain() {
    maintain_zip();
    maintain_transfer_cache();
    cache_.maintain();
    scripts_.maintain();
    inference_.maintain();
    {std::lock_guard<std::mutex> lock(type_mutex_);const auto now=std::chrono::steady_clock::now();for(auto it=types_.begin();it!=types_.end();)if(now-it->second.used>std::chrono::seconds(60))it=types_.erase(it);else ++it;}
    {std::lock_guard<std::mutex> lock(listing_mutex_); const auto now=std::chrono::steady_clock::now();
    for(auto it=listings_.begin();it!=listings_.end();) {
        if(now-it->second->created>std::chrono::seconds(5)) { listing_bytes_-=it->second->bytes; it=listings_.erase(it); }
        else ++it;
    }}
    maintain_durable();
    maintain_browsing();
    maintain_mounts();
    maintain_access();
    {std::lock_guard<std::mutex> lock(p2p_.mutex);p2p_.prune(settings()->p2p?settings()->access_revision:0);}
}
Response Service::error(unsigned status,const std::string& message) {
    Response response; response.status=status; response.body="{\"error\":"+json_quote(message)+"}";
    response.headers.emplace_back("Cache-Control","no-store");
    if(status==503) response.headers.emplace_back("Retry-After","1");
    return response;
}
namespace {
bool inside(const std::string& parent,const std::string& path) {
    return path==parent || (path.size()>parent.size() && path.compare(0,parent.size(),parent)==0 && path[parent.size()]=='/');
}
std::string logical_parameter(const Request& request) { return normalize_path(parameter(query_params(request.target),"path","/")); }
}
void Service::browser_guard(const Request& request,bool mutation) {
    // The listener supplies the numeric local socket authority; headers cannot grant privileges.
    auto host=lower_ascii(request.header("host"));
    // RFC 6874 interface-zone escaping in numeric IPv6 URLs.
    for(std::size_t at=0;(at=host.find("%25",at))!=std::string::npos;)host.replace(at,3,"%");
    const auto colon=request.authority.rfind(':');
    const auto actual_port=colon==std::string::npos?std::string():request.authority.substr(colon);
    const auto port=request.control_channel?actual_port:":"+std::to_string(config_.port);
    // IE does not consistently include the port in origin comparisons. Reserve
    // localhost for the control origin, and numeric IPs for application pages.
    if(request.control_channel&&host!="localhost"+actual_port)throw Failure(403,"Use the localhost host-control URL printed by the CLI");
    if(request.application_listener&&(host=="localhost"||host.rfind("localhost:",0)==0))throw Failure(403,"Applications must use a numeric server IP address");
    const bool trusted=host==lower_ascii(request.authority) || host=="localhost"+port || (config_.port==(request.secure?443:80) && host=="localhost");
    if(!trusted) throw Failure(403,"Use the server's numeric IP address or localhost");
    const auto origin=lower_ascii(request.header("origin"));
    if(!origin.empty() && origin!=std::string(request.secure?"https://":"http://")+lower_ascii(request.header("host"))) throw Failure(403,"Cross-origin access is denied");
    if(mutation && request.header("x-hls-token")!=token_) throw Failure(403,"Missing or invalid write token");
}
std::string Service::owner_root(const Request& request) const {
    std::string owner=request.peer_ip;
    if(owner.empty()) throw Failure(403,"Client address is unavailable");
    if(owner.find(':')!=std::string::npos) {
        std::string encoded="v6-";static const char hex[]="0123456789abcdef";
        for(unsigned char c:owner){encoded+=hex[c>>4];encoded+=hex[c&15];}owner=encoded;
    }
    return "/_clients/"+owner;
}
void Service::ensure_owner(const Request& request) {
    if(settings()->write_mode!="isolated") return;
    const auto own=owner_root(request);
    secure_mkdir(config_.root,"/_clients",true);
    std::error_code ec;
    if(std::filesystem::exists(resolve("/_clients")/std::filesystem::u8path(own.substr(10)),ec)) {
        secure_mkdir(config_.root,own,true);return;
    }
    std::size_t count=0;
    for(const auto& entry:std::filesystem::directory_iterator(resolve("/_clients"))) {
        const auto state=entry.symlink_status(ec);if(!ec&&std::filesystem::is_directory(state)&&!link_like(entry.path()))++count;
    }
    if(!request.local_admin && settings()->owner_limit && count>=settings()->owner_limit)throw Failure(503,"Isolated directory limit reached");
    secure_mkdir(config_.root,own,true); changed();
}
void Service::authorize(const Request& request,const std::string& path) {
    browser_guard(request,true);
    check_mount_write(request,path);
    protect_tls_key(mapped(path),true);
    protect_config(path);
    if(settings()->write_mode=="readonly") throw Failure(403,"This share is read-only");
    if(path=="/" || (settings()->write_mode=="isolated" && path=="/_clients")) throw Failure(403,"Managed root cannot be modified");
    if(settings()->write_mode=="isolated" && !request.local_admin) {
        const auto own=owner_root(request);
        if(!inside(own,path) || path==own) throw Failure(403,"Write access is limited to your IP directory");
    }
}
void Service::check_busy(const std::string& path) {
    for(auto it=uploads_.begin();it!=uploads_.end();) {
        if(it->second.expired()){it=uploads_.erase(it);continue;}
        if(inside(path,it->first) || inside(it->first,path)) throw Failure(409,"An upload is using this path");
        ++it;
    }
}
void Service::changed() {
    std::lock_guard<std::mutex> build_lock(build_mutex_);
    {std::lock_guard<std::mutex> lock(listing_mutex_);listings_.clear();listing_bytes_=0;}
    index_.refresh();
}
FileType Service::identify(const std::shared_ptr<File>& file,const std::string& path) {
    const bool use_model=settings()->magika;
    const auto key=file->info().version+":"+std::filesystem::u8path(path).extension().u8string()+(use_model?":model":":signature");
    {
        std::unique_lock<std::mutex> lock(type_mutex_);type_ready_.wait(lock,[&]{return identifying_.count(key)==0;});
        auto found=types_.find(key);
        if(found!=types_.end()){found->second.used=std::chrono::steady_clock::now();return found->second.type;}
        identifying_.insert(key);
    }
    try {
        auto type=use_model?detect_type(file,path,inference_):detect_type(file,path);
        {std::lock_guard<std::mutex> lock(type_mutex_);
            if(types_.size()>=512){auto oldest=std::min_element(types_.begin(),types_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});types_.erase(oldest);}
            types_[key]={type,std::chrono::steady_clock::now()};identifying_.erase(key);
        }
        type_ready_.notify_all();return type;
    }catch(...){ {std::lock_guard<std::mutex> lock(type_mutex_);identifying_.erase(key);}type_ready_.notify_all();throw; }
}
Response Service::session(const Request& request) {
    browser_guard(request,false);
    const bool writable=settings()->write_mode!="readonly";
    const auto own=settings()->write_mode=="isolated"&&!request.local_admin?owner_root(request):"/";
    Response response;response.headers.emplace_back("Cache-Control","no-store");
    response.body="{\"zipPreview\":"+json_quote(settings()->zip_preview)+",\"namespaceId\":"+json_quote(state_.get("browser-namespace",config_.root.u8string()))+",\"resumableUploads\":true,\"recycleBin\":"+std::string(settings()->recycle?"true":"false")+",\"mode\":"+json_quote(config_.mode)+",\"p2pCacheMiB\":"+std::to_string(settings()->p2p_cache_mb)+",\"p2pCacheSeconds\":"+std::to_string(settings()->p2p_cache_seconds)+",\"p2pEnabled\":"+std::string(settings()->p2p?"true":"false")+",\"hotlinkSeconds\":"+std::to_string(settings()->hotlink_seconds)+",\"clientIP\":"+json_quote(request.peer_ip)+",\"localAdmin\":"+(request.local_admin?"true":"false")+
        ",\"writeMode\":"+json_quote(settings()->write_mode)+",\"writable\":"+(writable?"true":"false")+",\"writableRoot\":"+json_quote(writable?own:"")+
        ",\"uploadRoot\":"+json_quote(settings()->write_mode=="isolated"&&!request.local_admin?owner_root(request):writable?own:"")+
        ",\"configBasename\":"+json_quote(config_store_.basename())+",\"mountRoot\":"+json_quote("/_mounts/"+owner_root(request).substr(10))+
        ",\"previewAllowed\":"+(settings()->preview?"true":"false")+",\"thumbnailsEnabled\":"+(settings()->thumbnails?"true":"false")+",\"mounts\":"+mounts_json()+",\"token\":"+json_quote(token_)+"}";
    return response;
}
std::shared_ptr<Upload> Service::begin_upload(const Request& request) {
    application_guard(request,true);
    if(!access_guard(request))throw Failure(401,"Login required");
    std::lock_guard<std::mutex> lock(write_mutex_);
    const auto parameters=query_params(request.target);auto path=logical_parameter(request);
    if(std::count(path.begin(),path.end(),'/')>65)throw Failure(400,"Upload directory depth exceeds 64");
    if(parameter(parameters,"drop")=="1"&&settings()->write_mode=="isolated"&&!request.local_admin){
        browser_guard(request,true);const auto own=owner_root(request);
        if(!inside(own,path))path=own+"/"+std::filesystem::u8path(path).filename().u8string();
    }
    const auto mirror_key=parameter(parameters,"mirror");Mount mount;const bool mirror=!mirror_key.empty()&&remote_mount(path,&mount);
    if(!mirror_key.empty()&&!mirror)throw Failure(409,"Browser mount was removed");
    if(mirror){browser_guard(request,true);if(mount.key!=mirror_key||mount.peer!=request.peer_ip)throw Failure(403,"Invalid mount replica writer");if(settings()->write_mode=="isolated"&&!request.local_admin&&!inside(owner_root(request),path))throw Failure(403,"Mount is outside the current IP write scope");}
    else{if(remote_mount(path))throw Failure(405,"Use browser mount upload chunks");authorize(request,path);}
    check_busy(path);preflight_quota(request);
    const auto length=request.header("content-length");
    script_file(request,"upload",path,length.empty()?0:parse_unsigned(length,UINT64_MAX));
    if(!length.empty() && parse_unsigned(length,UINT64_MAX)>settings()->max_upload_bytes)throw Failure(413,"Upload exceeds configured limit");
    auto version=parameter(parameters,"replaceVersion");if(version.size()>512||parameter(parameters,"sourceVersion").size()>512)throw Failure(400,"Invalid replacement version");
    protect_config(path);
    const auto storage=mapped(path);
    if(mirror){
        if(settings()->write_mode=="isolated"&&!request.local_admin)ensure_owner(request);
        for(auto slash=storage.path.find('/',1);slash!=std::string::npos;slash=storage.path.find('/',slash+1))secure_mkdir(storage.root,storage.path.substr(0,slash),true);
        try{version=File::open(storage.root,storage.path)->info().version;}catch(const Failure& e){if(e.status!=404)throw;}
    }else upload_parents(request,path,parameter(parameters,"parents")=="1");
    auto charge=upload_charge(request);
    if(mirror){auto quota=std::move(charge);charge=[this,path,mirror_key,quota](std::size_t bytes){Mount current;if(!remote_mount(path,&current)||current.key!=mirror_key)throw Failure(409,"Browser mount was removed");if(quota)quota(bytes);};}
    auto upload=std::make_shared<Upload>(storage.root,storage.path,settings()->max_upload_bytes,version,path,std::move(charge));uploads_[path]=upload;return upload;
}
Response Service::finish_upload(const Request& request,const std::shared_ptr<Upload>& upload) {
    if(!access_guard(request))throw Failure(401,"Login required");
    upload->prepare(); // Flush the private file without blocking unrelated mutations.
    std::shared_ptr<File> committed;
    {std::lock_guard<std::mutex> lock(write_mutex_);Mount mount;const auto mirror=parameter(query_params(request.target),"mirror");
        if(!mirror.empty()&&!remote_mount(upload->path(),&mount))throw Failure(409,"Browser mount was removed");
        if(!mirror.empty()){
            browser_guard(request,true);if(mount.key!=mirror||mount.peer!=request.peer_ip)throw Failure(403,"Mount provider changed");
            std::lock_guard<std::mutex> mounts(mount_mutex_);const auto current=mounts_.find(mount.path);const auto source=parameter(query_params(request.target),"sourceVersion");const auto old=current->second.mirrored.find(upload->path());const auto old_bytes=old==current->second.mirrored.end()?0:old->first.size()+old->second.source.size()+old->second.host.size()+128;const auto addition=upload->path().size()+source.size()+512;std::size_t total=0;for(const auto& item:mounts_)total+=item.second.metadata_bytes;
            if(current->second.metadata_bytes-old_bytes+addition>(8ull<<20)||total-old_bytes+addition>(32ull<<20))throw Failure(507,"Mount replica metadata budget reached");
        }
        else authorize(request,upload->path());
        const auto copying=query_params(request.target);const auto copy_id=copying.find("copy");
        if(copy_id!=copying.end()){const auto job=copy_jobs_.find(copy_id->second);if(job==copy_jobs_.end())throw Failure(409,"Copy job expired");const auto flag=job->second.cancelled.lock();if(!flag||flag->load())throw Failure(499,"Copy cancelled");}
        protect_config(upload->path());
        upload->commit();uploads_.erase(upload->path());changed();committed=open_file(upload->path());
        if(!mirror.empty()){std::lock_guard<std::mutex> mounts(mount_mutex_);auto it=mounts_.find(mount.path);if(it!=mounts_.end()){auto& versions=it->second.mirrored;const auto old=versions.find(upload->path());if(old!=versions.end())it->second.metadata_bytes-=old->first.size()+old->second.source.size()+old->second.host.size()+128;const auto source=parameter(query_params(request.target),"sourceVersion");versions[upload->path()]={source,committed->info().version};it->second.metadata_bytes+=upload->path().size()+source.size()+committed->info().version.size()+128;}}
    }
    if(settings()->write_mode=="isolated"&&!request.local_admin){
        std::lock_guard<std::mutex> lock(alias_mutex_);const auto own=owner_root(request);
        if(aliases_.count(own)||aliases_.size()<4096){aliases_[own].uploaded=true;persist_alias(own,&aliases_[own]);}
    }
    Response response;response.status=upload->replacing()?200:201;
    const auto name=std::filesystem::u8path(upload->path()).filename().u8string();
    response.body="{\"path\":"+json_quote(upload->path())+",\"size\":"+std::to_string(upload->size())+",\"version\":"+json_quote(committed->info().version)+",\"type\":"+type_json(identify(committed,name),name)+"}";
    return response;
}
Response Service::handle_request(const Request& request) {
    try {
        if(request.target.empty() || request.target.front()!='/' || request.target.size()>8192) return error(400,"Invalid request target");
        const auto question=request.target.find('?');const auto raw_path=request.target.substr(0,question);
        const auto path=normalize_path(url_decode(raw_path));
        const bool reading=request.method=="GET" || request.method=="HEAD";
        if(!config_.workspace_app.empty()&&request.control_channel){
            if(!request.local_admin)return error(403,"Host control accepts local clients only");
            if(path=="/"){Response redirect;redirect.status=302;redirect.headers.emplace_back("Location","/_hls/");return redirect;}
            const bool manager=path=="/_hls"||path.rfind("/_hls/",0)==0||path.rfind("/api/",0)==0||path=="/mcp"||path=="/download"||path=="/preview"||path.rfind("/view/",0)==0||path.rfind("/preview-assets/",0)==0||path.rfind("/zip-assets/",0)==0||path=="/favicon.ico"||path=="/favicon.svg"||path=="/apple-touch-icon.png";
            if(!manager||path.rfind("/api/app/",0)==0)return error(404,"Application code is not served on the host control origin");
        }
        if(!config_.workspace_app.empty()){
            const bool public_route=path=="/mcp"||path.rfind("/api/access/",0)==0||path.rfind("/api/app/",0)==0||
                (path.rfind("/api/",0)!=0&&path.rfind("/_hls",0)!=0&&path!="/download"&&path!="/preview"&&path.rfind("/view/",0)!=0&&path.rfind("/preview-assets/",0)!=0&&path.rfind("/zip-assets/",0)!=0);
            application_guard(request,!public_route);
        }
        if(path.rfind("/zip-assets/",0)==0)return zip_dispatch(request,path);
        if(path.rfind("/preview-assets/",0)==0){
            const auto prefix=std::string("/preview-assets/");const auto slash=path.find('/',prefix.size());
            if(slash==std::string::npos||slash-prefix.size()!=48||!reading)throw Failure(403,"Invalid preview resource address");
            auto next=request;next.preview_key=path.substr(prefix.size(),48);const auto logical=normalize_path(path.substr(slash));
            if(logical.rfind("/api",0)==0||logical=="/mcp"||logical=="/download"||logical=="/preview"||logical.rfind("/view",0)==0||logical.rfind("/preview-assets",0)==0)throw Failure(403,"Preview grants only cover shared file resources");
            if(!access_guard(next,true))throw Failure(401,"Preview access expired");
            std::string encoded;for(char c:logical)encoded+=c=='/'?"/":url_encode(std::string(1,c));
            next.target="/view"+encoded+(question==std::string::npos?"":request.target.substr(question));return handle(next);
        }

        if(path.rfind("/api/access/",0)==0||path=="/api/admin/access")return access_api(request,path);
        const bool shell=(config_.mode=="filetrans"&&path=="/")||path=="/_hls/"||path=="/_hls"||path=="/preview";
        if(path=="/mcp")return mcp(request);
        if(!access_guard(request)){if(reading&&(shell||path=="/"))return login_page(request);return error(401,"Login required");}
        const std::string preview_prefix="/_hls/preview-assets/";
        if(path=="/_hls/viewer"||path.rfind(preview_prefix,0)==0){
            if(!reading)throw Failure(405,"Preview resources are read-only");
            if(!settings()->preview)throw Failure(403,"Preview is disabled");
            const bool html=path=="/_hls/viewer",compressed=config_.compression&&accepts_gzip(lower_ascii(request.header("accept-encoding")));
            const auto asset_path=html?"viewer.html":path.substr(preview_prefix.size());
            auto asset=embedded_preview(asset_path,compressed);Response response;response.content_type=asset.content_type;
            response.headers={{"Cache-Control",html?"no-store":"private, no-cache"},{"ETag",asset.etag},{"Vary","Accept-Encoding"},{"X-Content-Type-Options","nosniff"},{"Referrer-Policy","no-referrer"}};
            if(html)response.headers.emplace_back("Content-Security-Policy","default-src 'self' data: blob:; script-src 'self' 'unsafe-eval' blob:; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self' data: blob:; worker-src 'self' blob:; connect-src 'self'; frame-src 'self' blob:; object-src 'none'; base-uri 'self'; frame-ancestors 'self'");
            const auto host_name=lower_ascii(request.header("host"));const auto preview_authority=(request.control_channel||host_name==lower_ascii(request.authority)||host_name=="localhost:"+std::to_string(config_.port)||host_name=="localhost"&&config_.port==(request.secure?443:80))?host_name:request.authority;
            if(asset_path=="zip-sandbox.html")response.headers.emplace_back("Content-Security-Policy",std::string("sandbox allow-scripts; default-src 'none'; script-src ")+(request.secure?"https://":"http://")+preview_authority+"/_hls/preview-assets/zip-sandbox.js blob:; style-src blob: 'unsafe-inline'; img-src blob: data:; font-src blob: data:; media-src blob: data:; connect-src 'none'; object-src 'none'; frame-src 'none'; worker-src 'none'; form-action 'none'; base-uri 'none'");
            if(etag_matches(request.header("if-none-match"),asset.etag))response.status=304;
            else{response.body=std::move(asset.body);if(compressed)response.headers.emplace_back("Content-Encoding","gzip");}return response;
        }
        if(!config_.workspace_app.empty()&&path.rfind("/api/app/",0)==0){Response response;if(script_request(request,response))return response;return error(404,"Workspace endpoint not found");}
        if(path.rfind("/api/zip/",0)==0)return zip_dispatch(request,path);
        if(path.rfind("/api/p2p/",0)==0)return p2p_api(request,path);
        if((path=="/api/copy"||path=="/api/copy-cancel"||path=="/api/copy-progress")&&request.method=="POST")return copy_file(request);
        if(path=="/api/urls")return urls_response(config_);
        if(path=="/api/qr")return qr_response(config_,request.target);
        if(path=="/api/admin/settings")return admin_settings(request);
        if(path=="/api/admin/attention")return admin_attention(request);
        if(path=="/api/admin/aliases")return aliases(request,true);
        if(path.rfind("/api/resume/",0)==0)return resume_api(request,path);
        if(path.rfind("/api/transfer-cache/",0)==0)return transfer_cache_api(request,path);
        if(path.rfind("/api/recycle",0)==0)return recycle_api(request,path);
        if(path=="/api/admin/state"||path=="/api/batch")return durable_api(request,path);
        if(path.rfind("/api/",0)!=0&&!shell&&path!="/download"&&path.rfind("/view/",0)!=0){Response scripted;if(script_request(request,scripted))return scripted;}
        if(config_.mode=="server"&&!request.control_channel&&path!="/_hls/"&&path!="/_hls"&&path.rfind("/api/",0)!=0&&path!="/download"&&path!="/preview"&&path.rfind("/view/",0)!=0){
            if(reading&&!request.mirror_local&&remote_mount(path))return mount_dispatch(request,path,path);
            const bool icon=path=="/favicon.ico"||path=="/favicon.svg"||path=="/apple-touch-icon.png";
            auto hosted=server_file(request,path);if(!icon||hosted.status!=404)return hosted;
        }
        if(path.rfind("/view/",0)==0&&request.preview_key.empty()&&(!settings()->login_password.empty()||settings()->hotlink_seconds))return preview_redirect(request,path.substr(5));
        const auto parameters=query_params(request.target);
        if(path=="/download"||path=="/api/download-ticket"||path=="/api/preview-text"||path.rfind("/view/",0)==0)if(!access_guard(request,true))throw Failure(401,"Login required");
        if(path=="/api/upload-target"&&!parameter(parameters,"size").empty()){auto checking=request;checking.headers["content-length"]=parameter(parameters,"size");preflight_quota(checking);}
        if(path.rfind("/api/mount",0)==0)return mounts_api(request,path);
        std::string mount_path=path.rfind("/view/",0)==0?normalize_path(path.substr(5)):path.rfind("/api/",0)==0||path=="/download"?normalize_path(parameter(parameters,"path","/")):path;
        protect_config(mount_path);
        if(!request.mirror_local&&remote_mount(mount_path))return mount_dispatch(request,path,mount_path);
        if(path=="/api/alias")return aliases(request,false);
        if(path=="/api/browse")return browse(request);
        if(!reading) {
            std::lock_guard<std::mutex> lock(write_mutex_);
            const auto logical=normalize_path(parameter(parameters,"path","/"));authorize(request,logical);check_busy(logical);script_file(request,request.method=="DELETE"?"delete":path=="/api/rename"?"rename":"mkdir",logical);
            Response response;
            if(path=="/api/mkdir" && request.method=="POST") {mkdir_path(logical);response.status=201;}
            else if(path=="/api/rename" && request.method=="POST") {
                const auto to=normalize_path(parameter(parameters,"to"));authorize(request,to);check_busy(to);
                const auto version=parameter(parameters,"version");if(!version.empty()&&open_file(logical)->info().version!=version)throw Failure(409,"File changed before move; refresh and try again");
                rename_path(logical,to);move_attention(logical,to);
            } else if(path=="/api/entry" && request.method=="DELETE") {
                const auto version=parameter(parameters,"version");
                if(!version.empty()&&open_file(logical)->info().version!=version)throw Failure(409,"Source changed after copying; source was retained");
                const bool empty=parameter(parameters,"empty")=="1";
                if(empty&&!std::filesystem::is_empty(resolve(logical)))throw Failure(409,"Source directory changed; remaining entries were retained");
                if(settings()->recycle&&!empty)recycle_path(request,logical);else remove_path(logical,!empty&&parameter(parameters,"recursive")=="1");move_attention(logical);
            }
            else return error(405,"Unsupported method or write operation");
            changed();response.body="{\"ok\":true}";return response;
        }
        if(shell) {
            if(path=="/preview"&&!settings()->preview)throw Failure(403,"Preview is disabled");
            Response response;response.content_type="text/html; charset=utf-8";const auto encoding=lower_ascii(request.header("accept-encoding"));
            const bool compressed=config_.compression&&accepts_gzip(encoding);
            response.body=embedded_page(compressed);
            const bool preview_shell=path=="/preview";
            response.headers={{"Cache-Control","no-cache"},{"X-Frame-Options",preview_shell?"SAMEORIGIN":"DENY"},{"Content-Security-Policy",std::string("default-src 'self'; script-src 'unsafe-inline'; worker-src 'self' blob:; style-src 'unsafe-inline'; img-src 'self' data: blob:; media-src 'self' blob:; frame-src 'self'; connect-src 'self'; base-uri 'none'; frame-ancestors ")+(preview_shell?"'self'":"'none'")}};response.headers.emplace_back("Vary","Accept-Encoding");if(compressed)response.headers.emplace_back("Content-Encoding","gzip");return response;
        }
        if(path=="/favicon.ico" || path=="/favicon.svg" || path=="/apple-touch-icon.png") {
            const auto encoding=lower_ascii(request.header("accept-encoding"));
            const bool compressed=config_.compression&&accepts_gzip(encoding);
            auto icon=embedded_icon(path,compressed);Response response;response.content_type=icon.content_type;
            // A weak validator identifies the same graphic for gzip and identity.
            response.headers={{"Cache-Control","public, max-age=86400"},{"ETag",icon.etag},{"Vary","Accept-Encoding"},{"X-Content-Type-Options","nosniff"}};
            if(etag_matches(request.header("if-none-match"),icon.etag))response.status=304;
            else {response.body=std::move(icon.body);if(compressed)response.headers.emplace_back("Content-Encoding","gzip");}
            return response;
        }
        if(path=="/api/session")return session(request);
        if(path=="/api/site")return site_settings();
        if(path=="/api/status")return status();
        if(path=="/api/list"){script_file(request,"list",normalize_path(parameter(parameters,"path","/")));return list(request,parameters);}
        if(path=="/api/search"){script_file(request,"list",normalize_path(parameter(parameters,"path","/")));return search(request,parameters);}
        if(path=="/api/type") {
            const auto logical=normalize_path(parameter(parameters,"path","/"));script_file(request,"read",logical);Response response;
            response.body=type_json(identify(open_file(logical),logical),std::filesystem::u8path(logical).filename().u8string());response.headers.emplace_back("Cache-Control","no-store");return response;
        }
        if(path=="/api/upload-target"){
            const auto logical=normalize_path(parameter(parameters,"path","/"));Response response;response.headers.emplace_back("Cache-Control","no-store");
            try{
                const auto resolved=resolve(logical);
                if(std::filesystem::is_directory(resolved))response.body="{\"exists\":true,\"directory\":true}";
                else {const auto file=open_file(logical);response.body="{\"exists\":true,\"directory\":false,\"version\":"+json_quote(file->info().version)+"}";}
            }catch(const Failure& e){if(e.status!=404)throw;response.body="{\"exists\":false}";}
            return response;
        }
        if(path=="/api/preview-text") {
            if(!settings()->preview)throw Failure(403,"Preview is disabled");
            const auto logical=normalize_path(parameter(parameters,"path","/"));
            const auto file=open_file(logical);script_file(request,"read",logical,file->info().size);Response response;
            const auto preview_version=parameter(parameters,"version");if(!preview_version.empty()&&preview_version!=file->info().version)throw Failure(409,"File changed; reopen the editor");
            const auto type=identify(file,logical);
            if(!type.text && type.mime.rfind("text/",0)!=0 && type.mime.find("json")==std::string::npos && type.mime.find("xml")==std::string::npos && type.mime.find("javascript")==std::string::npos)throw Failure(415,"This file is not textual");
            const auto length=static_cast<std::size_t>(std::min<std::uint64_t>(file->info().size,2*1024*1024));
            response.body.resize(length);if(length)file->read(0,response.body.data(),length);
            response.content_type="text/plain; charset=utf-8";
            if(length>=2 && static_cast<unsigned char>(response.body[0])==0xff && static_cast<unsigned char>(response.body[1])==0xfe)response.content_type="text/plain; charset=utf-16le";
            else if(length>=2 && static_cast<unsigned char>(response.body[0])==0xfe && static_cast<unsigned char>(response.body[1])==0xff)response.content_type="text/plain; charset=utf-16be";
            else if(!valid_utf8(response.body)) {
                auto trimmed=response.body;for(int i=0;i<3 && !trimmed.empty() && !valid_utf8(trimmed);++i)trimmed.pop_back();
                if(valid_utf8(trimmed))response.body=std::move(trimmed);else response.content_type="text/plain; charset=gb18030";
            }
            response.headers={{"Cache-Control","no-store"},{"X-HLS-Preview-Truncated",file->info().size>length?"1":"0"}};
            return response;
        }
        if(path=="/api/download-ticket") {
            const auto logical=normalize_path(parameter(parameters,"path","/"));const auto file=open_file(logical);script_file(request,"read",logical,file->info().size);
            Response response;response.body="{\"url\":"+json_quote("/download?path="+url_encode(logical)+"&version="+url_encode(file->info().version))+",\"size\":"+std::to_string(file->info().size)+",\"modified\":"+std::to_string(file->info().modified)+",\"version\":"+json_quote(file->info().version)+"}";
            response.headers.emplace_back("Cache-Control","no-store");return response;
        }
        if(path=="/download")return download(request,normalize_path(parameter(parameters,"path","/")),true,parameter(parameters,"version"));
        if(path.rfind("/view/",0)==0)return preview_asset(request,path,true);
        // Root-relative assets in a preview map only to ordinary files within
        // the shared root. Management routes and private names remain reserved.
        if(path.rfind("/api",0)!=0 && path!="/mcp" && path!="/download" && path!="/preview" && path.rfind("/view",0)!=0)
            return preview_asset(request,path,false);
        return error(404,"Resource not found");
    }catch(const Failure& e){return error(static_cast<unsigned>(e.status),e.what());}
    catch(const std::invalid_argument& e){return error(400,e.what());}
    catch(const std::filesystem::filesystem_error&){return error(403,"Filesystem entry is not accessible");}
    catch(const std::exception&){return error(500,"Request could not be completed");}
}
Response Service::download(const Request& request,const std::string& path,bool attachment,const std::string& expected_version) {
    if(!access_guard(request,true))throw Failure(401,"Login required");
    auto file=open_file(path); const auto& info=file->info();script_file(request,"read",path,info.size);
    if(!expected_version.empty() && expected_version!=info.version) throw Failure(409,"File changed; obtain a new download link instead of resuming the old version");
    const auto etag="W/\""+info.version+"\"";
    Response response; response.file=file; response.length=info.size; response.content_type=extension_type(path).mime;
    if((config_.mode=="filetrans"||attachment||request.target.rfind("/view/",0)==0)&&(!attachment||extension_type(path).confidence=="unknown")) response.content_type=identify(file,path).mime;
    response.headers={{"Accept-Ranges","bytes"},{"ETag",etag},{"Last-Modified",http_date(static_cast<std::time_t>(info.modified))},{"Cache-Control","no-cache"}};
    if(attachment) {
        const auto name=std::filesystem::u8path(path).filename().u8string();
        response.headers.emplace_back("Content-Disposition","attachment; filename=\"download\"; filename*=UTF-8''"+url_encode(name));
    }
    const auto if_none=request.header("if-none-match"), if_modified=request.header("if-modified-since");
    bool not_modified=!if_none.empty() && etag_matches(if_none,etag);
    if(if_none.empty() && !if_modified.empty()) { const auto since=parse_http_date(if_modified); not_modified=since>=0 && info.modified<=since; }
    if(not_modified) { response.status=304; response.file.reset(); response.length=0; return response; }
    // Browser links pin the representation's file version in the URI. A changed
    // version fails above, so an old date cannot splice a newly modified file.
    // Weak ETags never satisfy If-Range; ordinary unpinned links fall back to 200.
    const auto if_range=request.header("if-range");
    const bool pinned_date=!expected_version.empty() && if_range==http_date(static_cast<std::time_t>(info.modified));
    if(request.method=="GET" && (if_range.empty() || pinned_date)) {
        const auto range=parse_range(request.header("range"),info.size);
        if(range.state==RangeState::unsatisfiable) {
            auto failure=error(416,"Requested byte range is not satisfiable"); failure.headers.emplace_back("Content-Range","bytes */"+std::to_string(info.size)); failure.headers.emplace_back("Accept-Ranges","bytes"); return failure;
        }
        if(range.state==RangeState::valid) {
            response.status=206; response.offset=range.first; response.length=range.length;
            response.headers.emplace_back("Content-Range","bytes "+std::to_string(range.first)+"-"+std::to_string(range.first+range.length-1)+"/"+std::to_string(info.size));
        }
    }
    return response;
}
Response Service::list(const Request& request,const std::map<std::string,std::string>& parameters) {
    const auto path=normalize_path(parameter(parameters,"path","/"));
    note_browse(request,path,parameter(parameters,"browser"));
    const bool show_hidden=page_number(parameters,"showHidden",0,1)!=0;
    const auto stamp=directory_stamp(path,show_hidden);
    std::filesystem::path resolved;
    try{resolved=resolve(path);}catch(const Failure& e){if(e.status!=404||(!virtual_owner(request,path)&&!synthetic(path)))throw;}
    if(resolved.empty()||!std::filesystem::is_directory(resolved)) {
        if(virtual_owner(request,path)&&!synthetic(path)){auto response=entries_response({},0,0,false);response.body.pop_back();response.body+=",\"stamp\":"+json_quote(stamp)+",\"virtual\":true}";return response;}
        if(!synthetic(path))throw Failure(404,"Directory not found");
    }
    auto offset=page_number(parameters,"offset",0,config_.directory_limit);
    const auto limit=page_number(parameters,"limit",100,500);
    if(!limit)throw Failure(400,"Page limit must be positive");
    const auto focus_value=parameter(parameters,"focus");
    const auto focus=focus_value.empty()?std::string():normalize_path(focus_value);
    if(!focus.empty()){
        const auto slash=focus.rfind('/');const auto parent=slash==0?"/":focus.substr(0,slash);
        if(parent!=path)throw Failure(400,"Focus file must belong to the requested directory");
        open_file(focus);
    }
    const auto sort=parameter(parameters,"sort","name"), order=parameter(parameters,"order","asc");
    if((sort!="name" && sort!="size" && sort!="modified" && sort!="type") || (order!="asc" && order!="desc")) throw Failure(400,"Unsupported sort order");
    const auto key=path+"\n"+sort+"\n"+order+(show_hidden?"\nall":"\nvisible"); std::shared_ptr<Listing> listing;
    const auto now=std::chrono::steady_clock::now();
    const bool refresh=parameter(parameters,"refresh")=="1";
    if(!refresh) {
        std::lock_guard<std::mutex> lock(listing_mutex_); auto it=listings_.find(key);
        if(it!=listings_.end() && now-it->second->created<std::chrono::seconds(3)) listing=it->second;
    }
    std::unique_lock<std::mutex> build_lock(build_mutex_,std::defer_lock);
    if(!listing) {
        build_lock.lock();
        if(!refresh){std::lock_guard<std::mutex> lock(listing_mutex_);auto found=listings_.find(key);if(found!=listings_.end() && now-found->second->created<std::chrono::seconds(3))listing=found->second;}
    }
    if(!listing) {
        listing=std::make_shared<Listing>(); listing->created=now;
        std::error_code ec; std::filesystem::directory_iterator it,end;
        if(!resolved.empty()&&!(config_.system_root&&path=="/"&&mapped(path).root==config_.root))it=std::filesystem::directory_iterator(resolved,std::filesystem::directory_options::skip_permission_denied,ec);
        if(ec) throw Failure(403,"Cannot enumerate directory");
        while(it!=end) {
            const auto entry=*it; auto state=entry.symlink_status(ec);
            const bool hidden=config_store_.frozen(entry.path())||entry_hidden(entry.path());
            if(!ec && (show_hidden||!hidden) && !std::filesystem::is_symlink(state) && !link_like(entry.path()) && (std::filesystem::is_directory(state)||std::filesystem::is_regular_file(state))) {
                const auto name=entry.path().filename().u8string();
                if(valid_utf8(name)) {
                    if(listing->entries.size()>=config_.directory_limit || listing->bytes>(8ull<<20)) { listing->truncated=true; break; }
                    const auto logical=path=="/"?"/"+name:path+"/"+name;
                    try {
                        if(config_store_.frozen(entry.path())){
                            listing->entries.push_back({logical,name,std::filesystem::is_directory(state),0,0,hidden});
                            listing->bytes+=sizeof(DirectoryEntry)+logical.size()+name.size()+64;it.increment(ec);if(ec)ec.clear();continue;
                        }
                        normalize_path(logical); const auto storage=mapped(logical);resolve_path(storage.root,storage.path);
                        const bool directory=std::filesystem::is_directory(state); const auto bytes=directory?0:entry.file_size(ec);
                        listing->entries.push_back({logical,name,directory,ec?0:bytes,file_modified(entry),hidden});
                        listing->bytes+=sizeof(DirectoryEntry)+logical.size()+name.size()+64;
                    } catch(const std::exception&) {}
                }
            }
            it.increment(ec); if(ec) ec.clear();
        }
        mount_entries(path,listing->entries);
        // Apply visibility before sorting/counting/paging, including virtual mounts.
        listing->entries.erase(std::remove_if(listing->entries.begin(),listing->entries.end(),[&](DirectoryEntry& entry){
            entry.hidden=entry.hidden||frozen(entry.path);
            return !show_hidden&&entry.hidden;
        }),listing->entries.end());
        std::sort(listing->entries.begin(),listing->entries.end(),[&](const DirectoryEntry& a,const DirectoryEntry& b) {
            if(a.directory!=b.directory) return a.directory>b.directory;
            int comparison=0;
            if(sort=="size" && a.size!=b.size) comparison=a.size<b.size?-1:1;
            else if(sort=="modified" && a.modified!=b.modified) comparison=a.modified<b.modified?-1:1;
            else if(sort=="type") { auto x=lower_ascii(std::filesystem::u8path(a.name).extension().u8string()),y=lower_ascii(std::filesystem::u8path(b.name).extension().u8string()); if(x!=y) comparison=x<y?-1:1; }
            if(!comparison) { const auto x=lower_ascii(a.name),y=lower_ascii(b.name); comparison=x==y?(a.name<b.name?-1:a.name==b.name?0:1):(x<y?-1:1); }
            return order=="desc"?comparison>0:comparison<0;
        });
        if(listing->bytes<=(32ull<<20)) {
            std::lock_guard<std::mutex> lock(listing_mutex_);
            auto previous=listings_.find(key); if(previous!=listings_.end()) { listing_bytes_-=previous->second->bytes; listings_.erase(previous); }
            while(!listings_.empty() && (listing_bytes_+listing->bytes>(32ull<<20) || listings_.size()>=32)) { auto victim=std::min_element(listings_.begin(),listings_.end(),[](const auto& a,const auto& b){return a.second->created<b.second->created;}); listing_bytes_-=victim->second->bytes; listings_.erase(victim); }
            listings_[key]=listing; listing_bytes_+=listing->bytes;
        }
    }
    std::size_t focus_index=0;
    if(!focus.empty()){
        const auto found=std::find_if(listing->entries.begin(),listing->entries.end(),[&](const DirectoryEntry& entry){return entry.path==focus;});
        if(found==listing->entries.end())throw Failure(listing->truncated?409:404,listing->truncated?"File is outside the directory listing limit":"File is no longer in this directory");
        focus_index=static_cast<std::size_t>(found-listing->entries.begin());offset=(focus_index/limit)*limit;
    }
    std::vector<DirectoryEntry> page;
    const auto end=std::min(listing->entries.size(),offset+limit);
    for(auto i=offset;i<end;++i) page.push_back(listing->entries[i]);
    auto response=entries_response(page,listing->entries.size(),offset,listing->truncated);
    if(!focus.empty()){
        response.body.pop_back();response.body+=",\"focusIndex\":"+std::to_string(focus_index)+",\"focusPath\":"+json_quote(focus)+"}";
    }
    response.body.pop_back();response.body+=",\"stamp\":"+json_quote(stamp)+",\"mounts\":"+mounts_json()+"}";
    if(build_lock.owns_lock())build_lock.unlock();
    return decorate_entries(std::move(response));
}
Response Service::search(const Request& request,const std::map<std::string,std::string>& parameters) {
    const auto path=normalize_path(parameter(parameters,"path","/"));
    try{resolve(path);}catch(const Failure& e){if(e.status==404&&virtual_owner(request,path))return list(request,parameters);throw;}
    note_browse(request,path,parameter(parameters,"browser"));
    const auto offset=page_number(parameters,"offset",0,config_.index_limit),limit=page_number(parameters,"limit",100,500);
    const bool show_hidden=page_number(parameters,"showHidden",0,1)!=0;
    if(mapped(path).root!=config_.root||synthetic(path)){
        const auto needle=lower_ascii(parameter(parameters,"q"));std::vector<DirectoryEntry> matches;std::vector<std::pair<std::string,unsigned>> pending{{path,0}};std::size_t scanned=0,bytes=0;bool truncated=false;
        while(!pending.empty()&&scanned<config_.index_limit&&bytes<(8ull<<20)){
            const auto current=pending.back();pending.pop_back();std::vector<DirectoryEntry> entries;std::error_code ec;
            try{for(const auto& entry:std::filesystem::directory_iterator(resolve(current.first),std::filesystem::directory_options::skip_permission_denied)){
                if(++scanned>config_.index_limit)break;
                const auto state=entry.symlink_status(ec);if(ec||std::filesystem::is_symlink(state)||link_like(entry.path()))continue;
                const auto name=entry.path().filename().u8string();const bool hidden=config_store_.frozen(entry.path())||entry_hidden(entry.path());if(!show_hidden&&hidden)continue;
                if(!valid_utf8(name))continue;
                const bool placeholder=config_store_.frozen(entry.path());const auto raw=(current.first=="/"?"":current.first)+"/"+name;std::string logical;
                try{logical=placeholder?raw:normalize_path(raw);}catch(const std::invalid_argument&){continue;}
                const bool dir=std::filesystem::is_directory(state);if(!dir&&!std::filesystem::is_regular_file(state))continue;
                const auto length=dir||placeholder?0:entry.file_size(ec);entries.push_back({logical,name,dir,ec?0:length,placeholder?0:file_modified(entry),hidden});
            }}catch(const std::exception&){}
            mount_entries(current.first,entries);
            for(auto& entry:entries){entry.hidden=entry.hidden||frozen(entry.path);if(!show_hidden&&entry.hidden)continue;if(lower_ascii(entry.name).find(needle)!=std::string::npos){matches.push_back(entry);bytes+=entry.path.size()+entry.name.size()+sizeof(DirectoryEntry);}
                if(entry.directory&&!frozen(entry.path)&&current.second<32)pending.emplace_back(entry.path,current.second+1);
            }
        }
        truncated=!pending.empty()||scanned>=config_.index_limit||bytes>=(8ull<<20);std::sort(matches.begin(),matches.end(),[](const auto& a,const auto& b){return a.path<b.path;});
        std::vector<DirectoryEntry> page;for(auto i=offset;i<std::min(matches.size(),offset+limit);++i)page.push_back(matches[i]);
        auto response=entries_response(page,matches.size(),offset,truncated);response.body.pop_back();response.body+=",\"stamp\":"+json_quote(directory_stamp(path,show_hidden))+",\"mounts\":"+mounts_json()+"}";return decorate_entries(std::move(response));
    }
    const auto result=index_.search(parameter(parameters,"q"),path,offset,limit,show_hidden);
    auto response=entries_response(result.entries,result.total,offset,result.truncated,result.indexing);
    response.body.pop_back();response.body+=",\"stamp\":"+json_quote(directory_stamp(path,show_hidden))+"}";
    return decorate_entries(std::move(response));
}
Response Service::status() {
    const auto s=cache_.stats(); Response response;
    const auto script=scripts_.stats(); const auto& assets=script.assets;
    const auto model=inference_.statistics();
    const auto uptime=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-started_).count();
    std::ostringstream out;
    out << "{\"version\":\"" HLS_VERSION "\",\"mode\":" << json_quote(config_.mode) << ",\"rootName\":" << json_quote(config_.root.filename().u8string())
        << ",\"p2p\":{\"enabled\":" << (settings()->p2p?"true":"false") << ",\"reportedPeerBytes\":" << p2p_.reported_peer_bytes.load() << "}"
        << ",\"uptime\":" << uptime << ",\"connections\":" << runtime_.connections.load() << ",\"active\":" << runtime_.active.load()
        << ",\"receivedBytes\":" << runtime_.received_bytes.load() << ",\"requests\":" << runtime_.requests.load() << ",\"sentBytes\":" << runtime_.sent_bytes.load() << ",\"rejected\":" << runtime_.rejected.load()
        << ",\"threads\":{\"io\":" << runtime_.io_threads.load() << ",\"file\":" << runtime_.file_threads.load() << ",\"metadata\":" << runtime_.metadata_threads.load() << "}"
        << ",\"magika\":{\"enabled\":" << (settings()->magika?"true":"false") << ",\"model\":\"standard_v3_3\",\"kernel\":" << json_quote(model.kernel)
        << ",\"inferences\":" << model.inferences << ",\"active\":" << model.active << ",\"residentBytes\":" << model.resident_bytes << "}"
        << ",\"transferErrors\":" << runtime_.transfer_errors.load() << ",\"residentMemory\":" << resident_memory()
        << ",\"script\":{\"compilations\":" << script.compilations << ",\"bytecodeBytes\":" << script.bytecode
        << ",\"calls\":" << script.calls << ",\"runtimeCreated\":" << script.created << ",\"runtimeReused\":" << script.reused
        << ",\"runtimeRetired\":" << script.retired << ",\"active\":" << script.active << ",\"idleRuntimes\":" << script.idle
        << ",\"assets\":{\"hits\":" << assets.hits << ",\"misses\":" << assets.misses << ",\"coalesced\":" << assets.coalesced
        << ",\"extractions\":" << assets.extractions << ",\"residentBytes\":" << assets.bytes << ",\"limitBytes\":" << assets.limit
        << ",\"releasedBytes\":" << assets.released << ",\"entries\":" << assets.entries << ",\"loading\":" << assets.loading << "}}"
        << ",\"cache\":{\"hits\":" << s.hits << ",\"misses\":" << s.misses << ",\"coalesced\":" << s.coalesced << ",\"diskBytes\":" << s.disk_bytes
        << ",\"residentBytes\":" << s.resident_bytes << ",\"allocatedBytes\":" << s.allocated_bytes << ",\"targetBytes\":" << s.target_bytes
        << ",\"limitBytes\":" << config_.cache_bytes << ",\"bufferBytes\":" << config_.buffer_bytes << ",\"releasedBytes\":" << s.released_bytes
        << ",\"entries\":" << s.entries << ",\"idleSeconds\":" << config_.cache_idle << ",\"halfLifeSeconds\":" << config_.heat_half_life << "},\"index\":{\"enabled\":" << (index_.enabled()?"true":"false")
        << ",\"scanning\":" << (index_.scanning()?"true":"false") << ",\"count\":" << index_.count() << ",\"truncated\":" << (index_.truncated()?"true":"false") << ",\"failed\":" << (index_.failed()?"true":"false") << "}}";
    response.body=out.str(); response.headers.emplace_back("Cache-Control","no-store"); return response;
}
}
