// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <boost/asio/ip/address.hpp>
#include <algorithm>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/mount.h>
#endif

namespace hls {
namespace {
bool inside(const std::string& root,const std::string& path){return root=="/"||root==path||(path.size()>root.size()&&path.compare(0,root.size(),root)==0&&path[root.size()]=='/');}
std::vector<std::pair<std::string,std::filesystem::path>> volumes(){
    std::vector<std::pair<std::string,std::filesystem::path>> result;
#ifdef _WIN32
    const auto mask=GetLogicalDrives();for(unsigned i=0;i<26;++i)if(mask&(1u<<i)){
        std::wstring drive(1,static_cast<wchar_t>('A'+i));drive+=L":\\";
        if(GetDriveTypeW(drive.c_str())!=DRIVE_NO_ROOT_DIR)result.emplace_back(std::string(1,static_cast<char>('A'+i)),drive);
    }
#elif defined(__APPLE__)
    struct statfs* mounts=nullptr;const auto count=getmntinfo(&mounts,MNT_NOWAIT);
    for(int i=0;i<count&&result.size()<64;++i){const std::string path=mounts[i].f_mntonname;if(path=="/")result.emplace_back("root",path);else if(path.rfind("/Volumes/",0)==0)result.emplace_back(std::filesystem::path(path).filename().u8string(),path);}
#else
    std::ifstream input("/proc/self/mountinfo");std::string line;
    while(std::getline(input,line)&&result.size()<64){
        std::istringstream fields(line);std::string id,par,device,root,path,options;fields>>id>>par>>device>>root>>path>>options;
        const auto split=line.find(" - ");if(split==std::string::npos)continue;
        std::istringstream detail(line.substr(split+3));std::string type,source;detail>>type>>source;
        if(path!="/"&&source.rfind("/dev/",0)!=0)continue;
        std::string decoded;for(std::size_t i=0;i<path.size();++i){if(path[i]=='\\'&&i+3<path.size()&&path[i+1]>='0'&&path[i+1]<='7'&&path[i+2]>='0'&&path[i+2]<='7'&&path[i+3]>='0'&&path[i+3]<='7'){decoded+=static_cast<char>((path[i+1]-'0')*64+(path[i+2]-'0')*8+path[i+3]-'0');i+=3;}else decoded+=path[i];}
        result.emplace_back(decoded=="/"?"root":std::filesystem::path(decoded).filename().u8string(),decoded);
    }
    if(result.empty())result.emplace_back("root","/");
#endif
    return result;
}
}
Service::Mapped Service::mapped(const std::string& path)const{
    std::lock_guard<std::mutex> lock(mount_mutex_);const Mount* best=nullptr;
    for(const auto& item:mounts_)if(inside(item.first,path)&&(!best||item.first.size()>best->path.size()))best=&item.second;
    Mapped result{config_.root,path};
    if(best&&!best->remote)result={best->source,path==best->path?"/":best->path=="/"?path:path.substr(best->path.size())};
    protect_tls_key(result);return result;
}
void Service::protect_tls_key(const Mapped& storage,bool ancestors)const{
    const auto candidate=(storage.root/std::filesystem::u8path(storage.path.substr(1))).lexically_normal();
    if(scripts_.protects(candidate))throw Failure(403,"Configured script identity is private");
    if(state_.protects(candidate,ancestors))throw Failure(403,"Private operational state is not shareable");
    for(auto secret:{config_.https?config_.tls_key:std::filesystem::path{},scripts_.path()}){
        if(secret.empty())continue;secret=std::filesystem::absolute(secret).lexically_normal();
        for(;;){std::error_code ec;if(candidate==secret||std::filesystem::equivalent(candidate,secret,ec))throw Failure(403,"Private host file is not shareable");
            if(!ancestors||secret==secret.root_path()||secret.empty())break;secret=secret.parent_path();}
    }
}
void Service::protect_config(const std::string& path,bool descendants)const{const auto storage=mapped(path);config_store_.protect(storage.root/std::filesystem::u8path(storage.path.substr(1)),descendants,config_.directory_limit);}
bool Service::frozen(const std::string& path)const{try{const auto storage=mapped(path);return config_store_.frozen(storage.root/std::filesystem::u8path(storage.path.substr(1)));}catch(const Failure& e){if(e.status==403)return true;throw;}}
std::filesystem::path Service::resolve(const std::string& path)const{protect_config(path);const auto storage=mapped(path);return resolve_path(storage.root,storage.path);}
std::shared_ptr<File> Service::open_file(const std::string& path)const{protect_config(path);const auto storage=mapped(path);auto file=File::open(storage.root,storage.path);config_store_.protect(*file);return file;}
void Service::mkdir_path(const std::string& path,bool existing){protect_config(path);if(synthetic(path))return;const auto storage=mapped(path);secure_mkdir(storage.root,storage.path,existing);}
void Service::remove_path(const std::string& path,bool recursive){protect_config(path,true);const auto storage=mapped(path);secure_remove(storage.root,storage.path,recursive,config_.directory_limit);}
void Service::rename_path(const std::string& from,const std::string& to){protect_config(from,true);protect_config(to,true);const auto a=mapped(from),b=mapped(to);if(a.root!=b.root)throw Failure(409,"Moving across mounts is not supported; copy the file instead");secure_rename(a.root,a.path,b.path);}
bool Service::synthetic(const std::string& path)const{
    std::lock_guard<std::mutex> lock(mount_mutex_);
    for(const auto& item:mounts_)if(item.first!=path&&inside(path,item.first))return true;
    return config_.system_root&&path=="/";
}
void Service::mount_entries(const std::string& path,std::vector<DirectoryEntry>& entries)const{
    std::lock_guard<std::mutex> lock(mount_mutex_);
    for(const auto& item:mounts_){
        if(item.first==path||!inside(path,item.first))continue;
        const auto suffix=path=="/"?item.first.substr(1):item.first.substr(path.size()+1);const auto name=suffix.substr(0,suffix.find('/'));
        if(name.empty())continue;
        const auto logical=path=="/"?"/"+name:path+"/"+name;
        auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.path==logical;});
        if(found==entries.end())entries.push_back({logical,name,true,0,0,false});else found->directory=true;
    }
}
void Service::add_native_mount(const std::string& requested,const std::filesystem::path& source,bool writable){
    const auto path=normalize_path(requested);if(path=="/_clients"||path.rfind("/api",0)==0||path=="/mcp"||path=="/download"||path=="/preview"||path=="/view"||path.rfind("/preview-assets",0)==0)throw Failure(400,"Mount path is reserved");
    const auto real=canonical_root(source);if(!std::filesystem::is_directory(real))throw Failure(400,"Mount source must be a directory");
    std::lock_guard<std::mutex> lock(mount_mutex_);if(mounts_.size()>=64)throw Failure(409,"Mount capacity reached");
    if(mounts_.count(path))throw Failure(409,"Mount path already exists");
    Mount mount;mount.path=path;mount.source=real;mount.id=random_token();mount.writable=writable;mount.movable=writable;mounts_[path]=std::move(mount);
}
void Service::init_mounts(){
    if(!config_.ip_upload_overrides.empty()){
        auto next=std::make_shared<LiveSettings>(*settings());next->ip_upload_overrides.clear();
        for(const auto& limit:config_.ip_upload_overrides){boost::system::error_code ec;const auto address=boost::asio::ip::make_address(limit.first,ec);if(ec)throw Failure(400,"Invalid per-IP upload allowance address");next->ip_upload_overrides[address.to_string()]=limit.second;}
        std::shared_ptr<const LiveSettings> snapshot=next;std::atomic_store(&live_,std::move(snapshot));
    }
    for(const auto& mount:config_.mounts)add_native_mount(mount.path,mount.source,mount.writable);
    auto mount_volumes=[&](const std::string& prefix){
        const auto root=normalize_path(prefix);unsigned index=0;
        for(const auto& volume:volumes()){
            auto name=volume.first;try{normalize_path("/"+name);}catch(...){name="volume-"+std::to_string(++index);}
            auto path=(root=="/"?"":root)+"/"+name;
            {std::lock_guard<std::mutex> lock(mount_mutex_);if(mounts_.count(path))path+="-"+std::to_string(++index);}
            try{add_native_mount(path,volume.second,false);}catch(const std::exception&){}
        }
    };
    if(config_.system_root){
#ifdef _WIN32
        mount_volumes("/");
#else
        add_native_mount("/","/",false);
#endif
    }
    for(const auto& prefix:config_.root_mounts){
#ifdef _WIN32
        mount_volumes(prefix);
#else
        add_native_mount(prefix,"/",false);
#endif
    }
    for(const auto& prefix:config_.volume_mounts)mount_volumes(prefix);
}
bool Service::remote_mount(const std::string& path,Mount* result)const{
    std::lock_guard<std::mutex> lock(mount_mutex_);const Mount* best=nullptr;
    for(const auto& item:mounts_)if(inside(item.first,path)&&(!best||item.first.size()>best->path.size()))best=&item.second;
    if(!best||!best->remote)return false;
    // Request routing copies only the descriptor. The bounded replica version
    // table stays in the registry; only this file's replica descriptor is copied.
    if(result){Mount descriptor;descriptor.path=best->path;descriptor.id=best->id;descriptor.key=best->key;descriptor.peer=best->peer;descriptor.browser=best->browser;descriptor.source=best->source;descriptor.writable=best->writable;descriptor.movable=best->movable;descriptor.remote=best->remote;descriptor.expires=best->expires;const auto replica=best->mirrored.find(path);if(replica!=best->mirrored.end())descriptor.mirrored.emplace(*replica);*result=std::move(descriptor);}
    return true;
}
void Service::check_mount_write(const Request& request,const std::string& path){
    std::lock_guard<std::mutex> lock(mount_mutex_);const Mount* best=nullptr;
    for(const auto& item:mounts_){
        if(inside(path,item.first))throw Failure(403,"Mount roots and their ancestors cannot be changed");
        if(inside(item.first,path)&&(!best||item.first.size()>best->path.size()))best=&item.second;
    }
    if(best&&!best->writable)throw Failure(403,"This mount is read-only");
    if(config_.system_root&&!best)throw Failure(403,"System root is read-only");
    (void)request;
}
std::string Service::mounts_json()const{
    namespace json=boost::json;json::array result;std::lock_guard<std::mutex> lock(mount_mutex_);
    for(const auto& item:mounts_){const auto& m=item.second;result.push_back(json::object{{"path",m.path},{"id",m.id},{"browser",m.remote},{"writable",m.writable},{"movable",m.movable},{"online",!m.remote||m.expires>std::chrono::steady_clock::now()}});}
    return json::serialize(result);
}
}
