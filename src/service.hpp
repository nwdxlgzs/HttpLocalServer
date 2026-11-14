#pragma once
#include "cache.hpp"
#include "index.hpp"
#include "storage.hpp"
#include "file_type.hpp"
#include "live_settings.hpp"
#include "config_store.hpp"
#include "access_policy.hpp"
#include "p2p.hpp"
#include "state_store.hpp"
#include "script_engine.hpp"
#include "traffic.hpp"
#include "zip_archive.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <set>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <functional>

namespace hls {
struct RuntimeStats {
    std::atomic<std::size_t> connections{0}, active{0};
    std::atomic<std::size_t> active_transfers{0}, active_control{0};
    std::atomic<std::size_t> active_provider_control{0},active_provider_uploads{0};
    std::atomic<unsigned> io_threads{0}, file_threads{0}, metadata_threads{0};
    std::atomic<std::uint64_t> requests{0}, sent_bytes{0}, received_bytes{0}, rejected{0}, transfer_errors{0};
};
struct Request {
    std::string method, target, body;
    std::string peer_ip, authority;
    bool local_admin=false;
    bool local_device=false,application_listener=false,control_channel=false;
    bool secure=false;
    bool mirror_local=false,prepaid=false;
    std::string preview_key;
    std::map<std::string,std::string> headers;
    std::string header(const std::string& name) const { auto it=headers.find(name); return it==headers.end()?"":it->second; }
};
struct Response {
    unsigned status=200;
    std::string body, content_type="application/json; charset=utf-8";
    std::vector<std::pair<std::string,std::string>> headers;
    std::shared_ptr<File> file;
    std::uint64_t offset=0, length=0;
    std::function<void(std::function<void(Response)>)> deferred;
    std::function<void()> cancel;
    bool gzip_stream=false,deflate_stream=false;
};
class Service {
public:
    Service(const Config& config, WorkerPool& workers, RuntimeStats& runtime);
    ~Service(){zip_workers_.stop();}
    Response handle(const Request& request);
    void application_guard(const Request& request,bool files=false);
    void encode_response(const Request& request,Response& response);
    TrafficController& traffic(){return *traffic_;}
    std::shared_ptr<TrafficController> traffic_handle(){return traffic_;}
    void transfer_activity(const Request& request);
    std::shared_ptr<Upload> begin_upload(const Request& request);
    Response finish_upload(const Request& request,const std::shared_ptr<Upload>& upload);
    static Response error(unsigned status, const std::string& message);
    void start();
    void stop();
    void maintain();
    HotCache& cache() { return cache_; }
    std::shared_ptr<const LiveSettings> settings() const {return std::atomic_load(&live_);}
    const std::string& mcp_token() const {return mcp_token_;}
private:
    Response zip_dispatch(const Request& request,const std::string& route);
    Response zip_api(const Request& request,const std::string& route,const std::shared_ptr<std::atomic<bool>>& cancelled);
    void init_zip();
    void maintain_zip();
    struct ZipSession {
        std::string ip,path,version;
        std::uint64_t revision=0,work=0;
        std::shared_ptr<ZipArchive> archive;
        std::chrono::steady_clock::time_point used;
        struct Cached {std::string path;std::uint64_t size=0;std::weak_ptr<File> active;};
        std::map<std::string,Cached> cached;
    };
    std::mutex zip_mutex_;
    std::map<std::string,ZipSession> zip_sessions_;
    std::uint64_t zip_cache_bytes_=0;
    WorkerPool zip_workers_{0,32,1,15};
    struct Alias;
    Response durable_api(const Request& request,const std::string& route);
    Response resume_api(const Request& request,const std::string& route);
    Response transfer_cache_api(const Request& request,const std::string& route);
    Response publish_mount_cache(const Request&,const std::string&,const std::string&,const std::string&,const std::string&,std::shared_ptr<File>,std::shared_ptr<void>);
    void maintain_transfer_cache();
    std::mutex transfer_cache_mutex_;
    Response recycle_api(const Request& request,const std::string& route);
    Response server_file(const Request& request,const std::string& path);
    void maintain_durable();
    void restore_durable();
    void persist_login(const std::string& ip,const LoginAttempt& attempt);
    void persist_alias(const std::string& path,const Alias* value);
    bool script_request(const Request& request,Response& response);
    void script_file(const Request& request,const std::string& operation,const std::string& path,std::uint64_t size=0);
    void recycle_path(const Request& request,const std::string& path);
    std::mutex resume_mutex_;
    struct ResumeLock {std::atomic<bool> busy{false};};
    std::map<std::string,std::weak_ptr<ResumeLock>> resume_locks_;
    struct ResumeSummary {std::string ip;std::uint64_t size=0,expires=0;bool done=false;};
    std::map<std::string,ResumeSummary> resume_summary_;
    void complete_resume(const std::string& id,std::uint64_t expires);
    std::shared_ptr<ResumeLock> resume_lock(const std::string& id);
    Response handle_request(const Request& request);
    Response access_api(const Request& request,const std::string& route);
    Response login_page(const Request& request);
    Response preview_redirect(const Request& request,const std::string& path);
    struct PreviewGrant {std::string ip,session;std::uint64_t revision=0;std::chrono::steady_clock::time_point created,last;};
    std::map<std::string,PreviewGrant> preview_grants_;
    std::string new_access(const Request& request,Response& response,bool authenticated,std::uint64_t expected_revision=0);
    bool access_guard(const Request& request,bool download=false,bool activity=true);
    Response p2p_api(const Request& request,const std::string& route);
    Response p2p_join(const Request& request,const std::string& path,const std::string& version,std::uint64_t size,const std::string& source_mount="",const std::string& source_version="",const std::string& source_path="");
    std::shared_ptr<File> p2p_file(const P2PState::Peer& peer)const;
    P2PState p2p_;
    void maintain_access();
    struct AccessSession {std::string ip;std::uint64_t revision=0;bool authenticated=false;std::chrono::steady_clock::time_point created,last;};
    std::mutex access_mutex_;
    std::map<std::string,AccessSession> access_sessions_;
    std::map<std::string,LoginAttempt> login_attempts_;
    Response copy_file(const Request& request);
    struct CopyJob {std::string peer;std::weak_ptr<std::atomic<bool>> cancelled;std::shared_ptr<std::atomic<std::uint64_t>> progress;std::uint64_t total=0;};
    std::map<std::string,CopyJob> copy_jobs_;
    WorkerPool& copy_workers_;
    struct Mirror {std::string source,host;};
    struct Mount {std::string path,id,key,peer,browser;std::filesystem::path source;bool writable=false,movable=false,remote=false;std::chrono::steady_clock::time_point expires;std::map<std::string,Mirror> mirrored;std::size_t metadata_bytes=0;};
    struct Mapped {std::filesystem::path root;std::string path;};
    void protect_config(const std::string& path,bool descendants=false)const;
    bool frozen(const std::string& path)const;
    Mapped mapped(const std::string& path) const;
    void protect_tls_key(const Mapped& storage,bool ancestors=false) const;
    std::filesystem::path resolve(const std::string& path) const;
    std::shared_ptr<File> open_file(const std::string& path) const;
    void mkdir_path(const std::string& path,bool existing=false);
    void remove_path(const std::string& path,bool recursive);
    void rename_path(const std::string& from,const std::string& to);
    bool synthetic(const std::string& path) const;
    void mount_entries(const std::string& path,std::vector<DirectoryEntry>& entries) const;
    void init_mounts();
    void add_native_mount(const std::string& path,const std::filesystem::path& source,bool writable);
    void maintain_mounts();
    Response mounts_api(const Request& request,const std::string& route);
    Response mount_dispatch(const Request& request,const std::string& route,const std::string& path);
    Response remote_call(const std::string& mount,const std::string& job,const std::string& payload="",std::function<Response(Response)> transform={},std::shared_ptr<File> file={});
    void check_mount_write(const Request& request,const std::string& path);
    bool remote_mount(const std::string& path,Mount* result=nullptr) const;
    std::string mounts_json() const;
    std::map<std::string,Mount> mounts_;
    mutable std::mutex mount_mutex_;
    struct MountJob {std::string id,mount,json,payload;std::shared_ptr<File> file;std::function<void(Response)> done;std::chrono::steady_clock::time_point expires;bool sent=false;};
    std::map<std::string,MountJob> mount_jobs_;
    struct MountUpload {std::string id,mount,path,peer,version;std::uint64_t length=0,received=0;bool busy=false;std::chrono::steady_clock::time_point expires;};
    std::map<std::string,MountUpload> mount_uploads_;
    struct QuotaState {std::mutex mutex;std::map<std::string,std::uint64_t> used,reserved;};
    std::shared_ptr<QuotaState> quota_=std::make_shared<QuotaState>();
    void charge_upload(const std::string& ip,std::size_t bytes);
    void preflight_quota(const Request& request);
    std::function<void(std::size_t)> upload_charge(const Request& request);
    Response admin_settings(const Request& request);
    Response admin_attention(const Request& request);
    void move_attention(const std::string& from,const std::string& to="");
    Response site_settings();
    Response aliases(const Request& request,bool admin);
    Response decorate_entries(Response response);
    Response browse(const Request& request);
    void note_browse(const Request& request,const std::string& path,const std::string& id);
    std::string directory_stamp(const std::string& path,bool hidden);
    void maintain_browsing();
    bool virtual_owner(const Request& request,const std::string& path) const;
    void upload_parents(const Request& request,const std::string& path,bool recursive);
    Response preview_asset(const Request& request,const std::string& path,bool explicit_view);
    Response mcp(const Request& request);
    std::shared_ptr<const LiveSettings> live_;
    const std::string mcp_token_=random_token();
    struct Listing {
        std::vector<DirectoryEntry> entries;
        std::chrono::steady_clock::time_point created;
        std::size_t bytes=0;
        bool truncated=false;
    };
    Response download(const Request& request,const std::string& path,bool attachment,const std::string& expected_version="");
    Response list(const Request& request,const std::map<std::string,std::string>& parameters);
    Response search(const Request& request,const std::map<std::string,std::string>& parameters);
    Response status();
    Response session(const Request& request);
    void authorize(const Request& request,const std::string& path);
    void browser_guard(const Request& request,bool mutation);
    std::string owner_root(const Request& request) const;
    void ensure_owner(const Request& request);
    void changed();
    void check_busy(const std::string& path);
    FileType identify(const std::shared_ptr<File>& file,const std::string& path);
    Config config_; ConfigStore config_store_; StateStore state_; ScriptEngine scripts_; std::shared_ptr<TrafficController> traffic_=std::make_shared<TrafficController>(); RuntimeStats& runtime_; HotCache cache_; SearchIndex index_;
    magika::Engine inference_;
    const std::string token_=random_token();
    std::mutex write_mutex_, build_mutex_;
    struct BrowserLease {std::string ip;std::vector<std::string> paths;std::chrono::steady_clock::time_point expires;};
    std::mutex browse_mutex_, watch_mutex_, alias_mutex_;
    std::map<std::string,BrowserLease> browsers_;
    struct Watch {std::string stamp;std::chrono::steady_clock::time_point checked;};
    std::map<std::string,Watch> watches_;
    struct Alias {std::string approved,pending;std::uint64_t revision=0;bool uploaded=false;std::chrono::steady_clock::time_point submitted;};
    std::map<std::string,Alias> aliases_;
    std::atomic<std::uint64_t> alias_revision_{0};
    std::filesystem::directory_iterator cleanup_iterator_;
    std::map<std::string,std::weak_ptr<Upload>> uploads_;
    std::mutex type_mutex_;
    std::condition_variable type_ready_;
    std::set<std::string> identifying_;
    struct TypeEntry {FileType type;std::chrono::steady_clock::time_point used;};
    std::map<std::string,TypeEntry> types_;
    std::mutex listing_mutex_; std::map<std::string,std::shared_ptr<Listing>> listings_; std::size_t listing_bytes_=0;
    const std::chrono::steady_clock::time_point started_=std::chrono::steady_clock::now();
};
}
