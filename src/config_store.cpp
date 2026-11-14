// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "config_store.hpp"
#include "util.hpp"
#include "storage.hpp"
#include "runtime_files.hpp"
#include <boost/json.hpp>
#include <algorithm>
#include <fstream>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace hls {
namespace {
namespace json=boost::json;
constexpr std::size_t maximum=262144;
void safe_location(const std::filesystem::path& path){
    auto current=path.root_path();
    for(const auto& part:path.relative_path()){
        current/=part;if(link_like(current))throw Failure(403,"Configuration paths cannot contain links or reparse points");
    }
}
std::string scalar(const json::value& value){
    if(value.is_string())return std::string(value.as_string());
    if(value.is_uint64())return std::to_string(value.as_uint64());
    if(value.is_int64()&&value.as_int64()>=0)return std::to_string(value.as_int64());
    throw std::runtime_error("Configuration option must be text, an unsigned integer, a boolean or a supported array");
}
const std::map<std::string,std::pair<std::string,std::string>> switches={
    {"wasm",{"--wasm","--no-wasm"}},{"workspace-share",{"--workspace-share","--no-workspace-share"}},
    {"recycle-bin",{"--recycle-bin","--no-recycle-bin"}},
    {"preview",{"--allow-preview","--no-preview"}},{"lazy-thumbnails",{"--lazy-thumbnails","--no-lazy-thumbnails"}},
    {"p2p",{"--p2p","--no-p2p"}},{"compression",{"--compression","--no-compression"}},
    {"index",{"","--no-index"}},{"magika",{"","--no-magika"}},
    {"mcp",{"--mcp",""}},{"open",{"--open",""}},{"system-root",{"--system-root",""}},
    {"show-cli-qr",{"--show-cli-qr",""}},{"https",{"--https",""}},{"tls-self-signed",{"--tls-self-signed",""}}
};
json::object options_json(const Config& c,const LiveSettings& s){
    json::object o{{"root",c.root.u8string()},{"mode",c.mode},{"bind",c.bind},{"port",c.port},
        {"threads",c.adaptive_threads?"auto":"fixed"},{"io-max",c.io_max},{"file-max",c.file_max},{"metadata-max",c.metadata_max},{"thread-idle",c.thread_idle},
        {"max-connections",c.max_connections},{"max-transfers",c.max_transfers},{"ip-limit",s.ip_limit},{"owner-limit",s.owner_limit},
        {"write-mode",s.write_mode},{"attention-click",s.attention_click},{"max-upload-mb",s.max_upload_bytes>>20},{"ip-upload-mb",s.ip_upload_bytes>>20},
        {"cache-mb",c.cache_bytes>>20},{"buffer-mb",c.buffer_bytes>>20},{"block-kb",c.block_bytes>>10},{"index-limit",c.index_limit},{"directory-limit",c.directory_limit},
        {"cache-idle",c.cache_idle},{"heat-half-life",c.heat_half_life},{"index-interval",c.index_interval},{"timeout",c.timeout},
        {"preview",s.preview},{"lazy-thumbnails",s.thumbnails},{"magika",s.magika},{"mcp",s.mcp},{"index",c.index},{"open",c.open_browser},
        {"cpu-kernel",c.cpu_kernel},{"magika-threads",c.magika_threads},{"system-root",c.system_root},{"show-cli-qr",c.show_cli_qr},{"compression",c.compression},
        {"https",c.https},{"tls-self-signed",c.tls_self_signed},{"tls-handshakes",c.tls_handshakes},{"tls-handshake-timeout",c.tls_handshake_timeout},
        {"p2p",s.p2p},{"p2p-peers",c.p2p_peers},{"p2p-cache-mb",s.p2p_cache_mb},{"p2p-cache-seconds",s.p2p_cache_seconds},{"p2p-upload-kbps",c.p2p_upload_kbps},
        {"password",s.login_password},{"hotlink-seconds",s.hotlink_seconds}};
    o["zip-preview"]=s.zip_preview;
    o["recycle-bin"]=s.recycle;o["recycle-days"]=s.recycle_days;o["recycle-mb"]=s.recycle_bytes>>20;
    o["resume-hours"]=c.resume_hours;o["resume-mb"]=c.resume_bytes>>20;o["ip-transfers"]=s.ip_transfers;
    o["upload-kbps"]=s.upload_kbps;o["download-kbps"]=s.download_kbps;o["ip-upload-kbps"]=s.ip_upload_kbps;o["ip-download-kbps"]=s.ip_download_kbps;
    o["default-documents"]=c.default_documents;o["spa-fallback"]=c.spa_fallback;
    if(!c.workspace_app.empty())o["workspace-app"]=c.workspace_app.u8string();
    if(!c.state_dir.empty())o["state-dir"]=c.state_dir.u8string();if(!c.script_path.empty())o["script"]=c.script_path.u8string();
    o["workspace-control-port"]=c.workspace_control_port;
    o["workspace-cache-mb"]=c.workspace_cache_bytes>>20;
    o["workspace-access"]=c.workspace_access;o["workspace-share"]=c.workspace_share;o["workspace-public"]=c.workspace_public;o["script-fs"]=c.script_fs;
    o["wasm"]=c.wasm;o["wasm-memory-mb"]=c.wasm_memory_mb;o["wasm-instances"]=c.wasm_instances;o["wasm-instructions"]=c.wasm_instructions;
    json::array args,ips;for(const auto& v:c.workspace_args)args.emplace_back(v);for(const auto& v:c.workspace_allow_ips)ips.emplace_back(v);o["workspace-arg"]=std::move(args);o["workspace-allow-ip"]=std::move(ips);
    o["script-memory-mb"]=c.script_memory_mb;o["script-timeout-ms"]=c.script_timeout_ms;o["script-concurrency"]=c.script_concurrency;
    json::array headers,errors;for(const auto& v:c.response_headers)headers.emplace_back(v.first+"="+v.second);for(const auto& v:c.error_pages)errors.emplace_back(v.first+"="+v.second);o["response-header"]=std::move(headers);o["error-page"]=std::move(errors);
    if(c.fixed_io)o["io-threads"]=c.io_threads;if(c.fixed_file)o["file-threads"]=c.file_threads;if(c.fixed_metadata)o["metadata-threads"]=c.metadata_threads;
    if(!c.tls_cert.empty())o["tls-cert"]=c.tls_cert.u8string();if(!c.tls_key.empty())o["tls-key"]=c.tls_key.u8string();
    json::array ro,rw;for(const auto& mount:c.mounts)(mount.writable?rw:ro).emplace_back(mount.path+"="+mount.source.u8string());
    o["mount"]=std::move(ro);o["mount-rw"]=std::move(rw);
    json::array roots,volumes,limits;for(const auto& v:c.root_mounts)roots.emplace_back(v);for(const auto& v:c.volume_mounts)volumes.emplace_back(v);
    for(const auto& v:s.ip_upload_overrides)limits.emplace_back(v.first+"="+std::to_string(v.second>>20)+"MB");
    o["mount-root"]=std::move(roots);o["mount-volumes"]=std::move(volumes);o["ip-upload-limit"]=std::move(limits);return o;
}
void atomic_write(const std::filesystem::path& path,const std::string& text){
    safe_location(path);
    const auto temporary=path.parent_path()/std::filesystem::u8path(".hls-config-"+random_token());
    RuntimeFile transaction(temporary);
#ifdef _WIN32
    const auto handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)throw Failure(500,"Cannot create configuration transaction");
    DWORD written=0;const bool ok=WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(handle);
    CloseHandle(handle);transaction.opened();
    if(!ok||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temporary.c_str());throw Failure(500,"Cannot commit configuration; settings were not changed");
    }
    SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
#else
    const auto fd=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)throw Failure(500,"Cannot create configuration transaction");
    bool ok=true;std::size_t at=0;while(at<text.size()){const auto n=::write(fd,text.data()+at,text.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}at+=static_cast<std::size_t>(n);}
    if(fsync(fd)!=0)ok=false;::close(fd);transaction.opened();
    if(!ok||::rename(temporary.c_str(),path.c_str())!=0){::unlink(temporary.c_str());throw Failure(500,"Cannot commit configuration; settings were not changed");}
    const auto directory=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(directory>=0){fsync(directory);::close(directory);}
#endif
}
}
std::vector<std::string> config_arguments(Config& c,int argc,char** argv){
    std::vector<std::string> cli{argc?argv[0]:"HttpLocalServer"};bool disabled=false,info=false,chosen=false;
    auto path=std::filesystem::current_path()/"config.json";
    for(int i=1;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--no-config"){disabled=true;continue;}
        if(arg=="--config"||arg.rfind("--config=",0)==0){if(chosen)throw std::runtime_error("Only one --config source is allowed");chosen=true;std::string name;if(arg=="--config"){if(++i>=argc)throw std::runtime_error("Missing --config path");name=argv[i];}else name=arg.substr(9);if(name.empty())throw std::runtime_error("Empty --config path");path=std::filesystem::u8path(name);continue;}
        if(arg=="--help"||arg=="-h"||arg=="--version"||arg=="--licenses")info=true;
        cli.push_back(arg);
    }
    if(disabled&&chosen)throw std::runtime_error("Choose --config or --no-config");
    if(disabled||info)return cli;
    path=std::filesystem::absolute(path).lexically_normal();if(path.u8string().size()>4096)throw std::runtime_error("Configuration path is too long");
    safe_location(path);path=std::filesystem::canonical(path.parent_path())/path.filename();c.config_path=path;c.config_explicit=chosen;
    std::error_code ec;const bool present=std::filesystem::exists(path,ec);if(ec)throw std::runtime_error("Cannot inspect configuration file");if(!present)return cli;
    const auto file=File::open(path.parent_path(),"/"+path.filename().u8string());
    if(file->info().size>maximum)throw std::runtime_error("Configuration exceeds 256 KiB");
    std::string bytes(static_cast<std::size_t>(file->info().size),'\0');if(file->read(0,bytes.data(),bytes.size())!=bytes.size())throw std::runtime_error("Cannot read configuration");
    if(bytes.rfind("\xef\xbb\xbf",0)==0)bytes.erase(0,3);
    json::parse_options policy;policy.max_depth=8;boost::system::error_code error;const auto parsed=json::parse(bytes,error,{},policy);
    if(error||!parsed.is_object())throw std::runtime_error("Invalid configuration JSON");const auto& document=parsed.as_object();
    for(const auto& member:document)if(member.key()!="version"&&member.key()!="options"&&member.key()!="host"&&member.key()!="protectedFileIds")throw std::runtime_error("Unknown configuration field: "+std::string(member.key()));
    if(const auto* version=document.if_contains("version"))if(!version->is_int64()||version->as_int64()!=1)throw std::runtime_error("Unsupported configuration version");
    if(const auto* ids=document.if_contains("protectedFileIds")){
        if(!ids->is_array()||ids->as_array().size()>1024)throw std::runtime_error("Invalid protected identity list");
        for(const auto& id:ids->as_array()){if(!id.is_string()||id.as_string().size()>128)throw std::runtime_error("Invalid protected identity");c.protected_file_ids.emplace_back(id.as_string());}
    }
    if(const auto* host=document.if_contains("host")){
        if(!host->is_object())throw std::runtime_error("Expected host settings object");
        for(const auto& member:host->as_object()){
            const auto key=std::string(member.key());const auto& value=member.value();
            if(key=="title"||key=="announcement"){
                if(!value.is_string()||value.as_string().size()>(key=="title"?128:4096)||!valid_utf8(std::string(value.as_string()))||value.as_string().find('\0')!=json::string::npos)throw std::runtime_error("Invalid host text");
                if(key=="title"){c.title=std::string(value.as_string());if(c.title.empty())throw std::runtime_error("Empty site title");}else c.announcement=std::string(value.as_string());
            }else if(key=="attentionPaths"){
                if(!value.is_array()||value.as_array().size()>128)throw std::runtime_error("Invalid attention files");
                for(const auto& entry:value.as_array()){if(!entry.is_string())throw std::runtime_error("Invalid attention path");c.attention_paths.push_back(normalize_path(std::string(entry.as_string())));}
            }else throw std::runtime_error("Unknown host configuration field: "+key);
        }
    }
    std::vector<std::string> result{cli[0]};
    if(const auto* settings=document.if_contains("options")){
        if(!settings->is_object()||settings->as_object().size()>128)throw std::runtime_error("Expected configuration options object");
        for(const auto& member:settings->as_object()){
            const auto key=std::string(member.key());const auto& value=member.value();
            if(key=="workspace-arg"||key=="workspace-allow-ip"){
                bool overridden=false;for(const auto& token:cli)if(token=="--"+key||token.rfind("--"+key+"=",0)==0||(key=="workspace-arg"&&token=="--workspace-args-clear"))overridden=true;
                if(overridden)continue;
            }
            if(key.empty()||key.front()=='-'||key=="strict-host"||key=="config"||key=="no-config"||key=="help"||key=="version"||key=="licenses")throw std::runtime_error("Configuration option is reserved: "+key);
            const auto flag=switches.find(key);
            if(flag!=switches.end()){
                if(!value.is_bool())throw std::runtime_error("Expected boolean for "+key);
                const auto& option=value.as_bool()?flag->second.first:flag->second.second;if(!option.empty())result.push_back(option);continue;
            }
            auto add=[&](const json::value& item){auto text=scalar(item);if(text.size()>8192||!valid_utf8(text)||text.find('\0')!=std::string::npos)throw std::runtime_error("Configuration value is too long or contains invalid text");
                auto relative=[&](const std::string& name){auto p=std::filesystem::u8path(name);return (p.is_absolute()?p:path.parent_path()/p).lexically_normal().u8string();};
                if(key=="root"||key=="tls-cert"||key=="tls-key"||key=="state-dir"||key=="script"||key=="workspace-app")text=relative(text);
                if(key=="mount"||key=="mount-rw"){const auto split=text.find('=');if(split!=std::string::npos)text=text.substr(0,split+1)+relative(text.substr(split+1));}
                result.push_back("--"+key+"="+text);
            };
            if(value.is_array()){
                if((key!="mount"&&key!="mount-rw"&&key!="mount-root"&&key!="mount-volumes"&&key!="ip-upload-limit"&&key!="response-header"&&key!="error-page"&&key!="workspace-arg"&&key!="workspace-allow-ip")||value.as_array().size()>256)throw std::runtime_error("Unsupported configuration array: "+key);
                for(const auto& item:value.as_array())add(item);
            }else add(value);
        }
    }
    result.insert(result.end(),cli.begin()+1,cli.end());return result;
}
ConfigStore::ConfigStore(const Config& config,bool lock):path_(config.config_path.empty()?std::filesystem::current_path()/"config.json":config.config_path),persistent_(!config.config_path.empty()),lock_enabled_(lock),retired_(config.protected_file_ids.begin(),config.protected_file_ids.end()){
    if(enabled()){depend(path_);depend(path_.parent_path()/std::filesystem::u8path(".hls-config-lock-"+path_.filename().u8string()));}
    if(enabled()&&lock&&exists())acquire_lock();
    try{std::error_code ec;if(enabled()&&std::filesystem::exists(path_,ec))identity_=File::open(path_.parent_path(),"/"+path_.filename().u8string())->info().identity;}
    catch(...){
#ifdef _WIN32
        if(lock_handle_!=-1)CloseHandle(reinterpret_cast<HANDLE>(lock_handle_));
#else
        if(lock_handle_!=-1)::close(static_cast<int>(lock_handle_));
#endif
        throw;
    }
}
void ConfigStore::acquire_lock(){
    if(!enabled()||!lock_enabled_||lock_handle_!=-1)return;
        safe_location(path_);
        const auto lock=path_.parent_path()/std::filesystem::u8path(".hls-config-lock-"+path_.filename().u8string());
#ifdef _WIN32
        const auto handle=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw Failure(409,"Configuration is already in use or its directory is not writable");
        OVERLAPPED region{};
        if(!LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&region)){CloseHandle(handle);throw Failure(409,"Configuration is already in use");}
        lock_handle_=reinterpret_cast<std::intptr_t>(handle);
#else
        const auto fd=::open(lock.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)throw Failure(403,"Cannot lock configuration directory");
        if(flock(fd,LOCK_EX|LOCK_NB)!=0){::close(fd);throw Failure(409,"Configuration is already in use");}lock_handle_=fd;
#endif
}
ConfigStore::~ConfigStore(){
#ifdef _WIN32
    if(lock_handle_!=-1)CloseHandle(reinterpret_cast<HANDLE>(lock_handle_));
#else
    if(lock_handle_!=-1)::close(static_cast<int>(lock_handle_));
#endif
}
bool ConfigStore::exists()const{if(!enabled())return false;std::error_code ec;return std::filesystem::exists(path_,ec);}
bool ConfigStore::frozen_unlocked(const std::filesystem::path& path)const{
    const auto real=std::filesystem::absolute(path).lexically_normal();
    if(runtime_file_frozen(real))return true;
    for(const auto& dependency:dependencies_)if(path_within(dependency.path,real))return true;
    std::error_code ec;if(enabled()&&std::filesystem::equivalent(path,path_,ec))return true;
    if(std::filesystem::is_regular_file(std::filesystem::symlink_status(path,ec))&&!link_like(path)){
        const auto id=file_identity(path);if(!id.empty()&&(id==identity_||retired_.count(id)||dependency_ids_.count(id)||runtime_file_identity(id)))return true;
    }
    return false;
}
void ConfigStore::depend(const std::filesystem::path& path,bool directory){
    if(path.empty())return;const auto real=std::filesystem::absolute(path).lexically_normal();std::string id;
    try{id=file_identity(real);}catch(const Failure& e){if(e.status!=403)throw;}
    std::lock_guard<std::mutex> lock(mutex_);
    for(auto& dependency:dependencies_)if(dependency.path==real){dependency.directory=dependency.directory||directory;if(!id.empty())dependency_ids_.insert(id);return;}
    if(dependencies_.size()>=1024)throw Failure(507,"Runtime dependency budget exhausted");dependencies_.push_back({real,directory});if(!id.empty())dependency_ids_.insert(id);
}
std::vector<ConfigStore::Dependency> ConfigStore::dependencies()const{std::lock_guard<std::mutex> lock(mutex_);return dependencies_;}
bool ConfigStore::frozen(const std::filesystem::path& path)const{std::lock_guard<std::mutex> lock(mutex_);return frozen_unlocked(path);}
void ConfigStore::protect(const File& file)const{std::lock_guard<std::mutex> lock(mutex_);if((file.info().identity==identity_||retired_.count(file.info().identity)||dependency_ids_.count(file.info().identity)||runtime_file_identity(file.info().identity)))throw Failure(403,"Configuration file is frozen");}
void ConfigStore::protect(const std::filesystem::path& path,bool descendants,std::size_t limit)const{
    std::lock_guard<std::mutex> lock(mutex_);
    if(frozen_unlocked(path))throw Failure(403,"Configuration file is frozen: no web operations are permitted");
    if(!descendants)return;
    std::error_code ec;if(!std::filesystem::is_directory(std::filesystem::symlink_status(path,ec)))return;
    // Preflight the whole mutation, so deleting/moving an ancestor cannot erase
    // a protected hard link under a different filename or another mount.
    std::filesystem::recursive_directory_iterator it(path,ec),end;if(ec)throw Failure(403,"Cannot validate protected descendants");
    for(std::size_t count=0;it!=end;it.increment(ec)){
        if(ec||++count>limit||it.depth()>128)throw Failure(413,"Protected directory traversal exceeds budget");
        if(link_like(it->path())){it.disable_recursion_pending();continue;}
        if(frozen_unlocked(it->path()))throw Failure(403,"Directory contains frozen configuration; operation skipped");
    }
    if(ec)throw Failure(403,"Cannot validate protected descendants");
}
void ConfigStore::save(const Config& config,const LiveSettings& live){
    if(!enabled())return;std::lock_guard<std::mutex> lock(mutex_);acquire_lock();safe_location(path_);
    auto next=retired_;std::error_code ec;
    bool same_identity=false;
    if(std::filesystem::exists(path_,ec)){
        const auto current=File::open(path_.parent_path(),"/"+path_.filename().u8string());
        same_identity=current->info().identity==identity_;
        if(std::filesystem::hard_link_count(path_,ec)>1)next.insert(current->info().identity);
        if(ec)throw Failure(500,"Cannot inspect configuration aliases");
    }
    if(!same_identity&&!identity_.empty())next.insert(identity_);
    if(next.size()>1024)throw Failure(507,"Configuration alias identity budget exhausted");
    json::array ids,attention;for(const auto& id:next)ids.emplace_back(id);for(const auto& path:live.attention_paths)attention.emplace_back(path);
    json::object document{{"version",1},{"options",options_json(config,live)},{"host",json::object{{"title",live.title},{"announcement",live.announcement},{"attentionPaths",std::move(attention)}}},{"protectedFileIds",std::move(ids)}};
    // Keep readable formatting without relying on a separate JSON dependency.
    std::string text="{\n  \"version\": 1,\n  \"options\": {\n";bool comma=false;
    for(const auto& member:document.at("options").as_object()){if(comma)text+=",\n";comma=true;text+="    "+json_quote(std::string(member.key()))+": "+json::serialize(member.value());}
    text+="\n  },\n  \"host\": "+json::serialize(document.at("host"))+",\n  \"protectedFileIds\": "+json::serialize(document.at("protectedFileIds"))+"\n}\n";
    if(text.size()>maximum)throw Failure(413,"Configuration exceeds 256 KiB");
    atomic_write(path_,text);retired_=std::move(next);identity_=File::open(path_.parent_path(),"/"+path_.filename().u8string())->info().identity;
}
}
