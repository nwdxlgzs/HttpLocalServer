#include "service.hpp"
#include "file_type.hpp"
#include "util.hpp"
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <boost/json.hpp>
using namespace hls;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string field(const std::string& json,const std::string& key){const auto start=json.find("\""+key+"\":\"");if(start==std::string::npos)throw std::runtime_error("Missing JSON field");const auto at=start+key.size()+4;return json.substr(at,json.find('"',at)-at);}
Request req(std::string method,std::string target,bool local=false,std::string ip="192.0.2.10"){
    Request r;r.method=method;r.target=target;r.peer_ip=ip;r.local_admin=local;r.authority="127.0.0.1:8000";r.headers["host"]=r.authority;return r;
}
void writes(const std::filesystem::path& root){
    WorkerPool workers(1,16);RuntimeStats stats;Config c;c.root=root;c.index=false;c.write_mode="isolated";c.owner_limit=2;
    Service service(c,workers,stats);
    auto r=req("GET","/api/session");auto session=service.handle(r);check(session.status==200,"Isolated session created");const auto token=field(session.body,"token");
    check(field(session.body,"writableRoot")=="/_clients/192.0.2.10","Peer-derived owner root");
    auto call=[&](std::string method,std::string target,bool local=false,std::string ip="192.0.2.10"){auto request=req(method,target,local,ip);request.headers["x-hls-token"]=token;return service.handle(request);};
    check(!std::filesystem::exists(root/"_clients"),"Opening a session never creates IP folders");
    check(call("POST","/api/mkdir?path=/_clients/192.0.2.10/dir").status==404,"Creating an empty folder cannot create an owner");
    auto seed=req("PUT","/api/upload?path=/_clients/192.0.2.10/seed.txt");seed.headers["x-hls-token"]=token;auto initial=service.begin_upload(seed);initial->append("seed",4);service.finish_upload(seed,initial);initial.reset();
    check(call("POST","/api/mkdir?path=/_clients/192.0.2.10/dir").status==201,"Own mkdir allowed");
    check(call("POST","/api/mkdir?path=/forbidden").status==403,"Root write denied remotely");
    check(call("POST","/api/mkdir?path=/_clients/192.0.2.100/hijack").status==403,"Prefix collision denied");
    check(call("DELETE","/api/entry?path=/_clients/192.0.2.10&recursive=1").status==403,"Own managed root protected");
    check(call("POST","/api/mkdir?path=/admin",true).status==201,"Local administrator writes at root");
    check(call("POST","/api/rename?path=/admin&to=/_clients/192.0.2.10/stolen").status==403,"Rename source authorization");
    check(call("POST","/api/rename?path=/_clients/192.0.2.10/dir&to=/escape").status==403,"Rename destination authorization");
    check(call("POST","/api/rename?path=/_clients/192.0.2.10/dir&to=/_clients/192.0.2.10/new").status==200,"Own directory rename");
    auto upload=req("PUT","/api/upload?path=/_clients/192.0.2.10/new/picture",false);upload.headers["x-hls-token"]=token;
    auto sink=service.begin_upload(upload);const std::string png="\x89PNG\r\n\x1a\n";sink->append(png.data(),png.size());
    check(call("DELETE","/api/entry?path=/_clients/192.0.2.10/new&recursive=1").status==409,"Upload pins mutation subtree");
    const auto hidden=call("GET","/api/list?path=/_clients/192.0.2.10/new&showHidden=1");
    auto listing=boost::json::parse(hidden.body).as_object().at("entries").as_array();bool protected_temp=false;
    for(const auto& value:listing){const auto& item=value.as_object();if(std::string(item.at("name").as_string()).rfind(".hls-upload-",0)==0){protected_temp=item.at("frozen").as_bool();auto temporary=std::string(item.at("path").as_string());check(call("GET","/download?path="+url_encode(temporary)).status==403,"Active transaction bytes cannot be downloaded");}}
    check(protected_temp,"Actual upload temporary is visibly frozen on explicit hidden listing");

    auto result=service.finish_upload(upload,sink);check(result.status==201 && result.body.find("picture.png")!=std::string::npos,"Signature detection and suffix suggestion");
    bool conflict=false;try{service.begin_upload(upload);}catch(const Failure& e){conflict=e.status==409;}check(conflict,"Upload never overwrites existing files");sink.reset();
    check(call("DELETE","/api/entry?path=/_clients/192.0.2.10/new&recursive=1").status==200,"Bounded recursive deletion");
    check(service.handle(req("POST","/api/mkdir?path=/admin2",true)).status==403,"CSRF token enforced for local administrator");
    auto bad=req("POST","/api/mkdir?path=/admin2",true);bad.headers["x-hls-token"]=token;bad.headers["origin"]="http://evil.test";check(service.handle(bad).status==403,"Cross-origin write rejected");
    bad=req("GET","/api/session",true);bad.headers["host"]="evil.test:8000";check(service.handle(bad).status==403,"DNS rebinding token access blocked");
    check(service.handle(req("GET","/api/session",false,"192.0.2.11")).status==200,"Second isolated owner allowed");
    check(service.handle(req("GET","/api/session",false,"192.0.2.12")).status==200,"Owner cap does not reject visitors");
    seed.peer_ip="192.0.2.11";seed.target="/api/upload?path=/_clients/192.0.2.11/seed.txt";initial=service.begin_upload(seed);initial->append("seed",4);service.finish_upload(seed,initial);initial.reset();
    seed.peer_ip="192.0.2.12";seed.target="/api/upload?path=/_clients/192.0.2.12/seed.txt";bool limited=false;try{service.begin_upload(seed);}catch(const Failure& e){limited=e.status==503;}check(limited,"Owner cap applies when an upload creates the folder");
    for(const auto& path:{"/../outside","/a/../../b","/CON.txt","/nul","/COM1.bin","/a:stream","/x\\escape"})check(call("POST","/api/mkdir?path="+url_encode(path),true).status==400,"Unsafe mutation path rejected");
    c.write_mode="readonly";c.state_dir=root/".hls-readonly-test-state";auto readonly=std::make_unique<Service>(c,workers,stats);auto ro=req("POST","/api/mkdir?path=/no",true);ro.headers["x-hls-token"]=field(readonly->handle(req("GET","/api/session",true)).body,"token");check(readonly->handle(ro).status==403,"Read-only policy includes localhost");
    c.write_mode="shared";c.state_dir=root/".hls-shared-test-state";auto shared=std::make_unique<Service>(c,workers,stats);auto sh=req("POST","/api/mkdir?path=/shared",false,"192.0.2.99");sh.headers["x-hls-token"]=field(shared->handle(req("GET","/api/session")).body,"token");check(shared->handle(sh).status==201,"Shared mode remote root writes");
}
void copy_cancellation(const std::filesystem::path& base){
    const auto root=base/"copy-cancellation";std::filesystem::create_directory(root);{std::ofstream(root/"source.txt")<<"copy source";}
    WorkerPool workers(1,16);RuntimeStats runtime;Config config;config.root=canonical_root(root);config.index=false;config.magika=false;config.write_mode="shared";Service service(config,workers,runtime);
    const auto token=field(service.handle(req("GET","/api/session")).body,"token");
    auto copy=req("POST","/api/copy?path=/source.txt&to=/cancelled.txt&copy=copy-test-cancellation-12345");copy.headers["x-hls-token"]=token;
    auto pending=service.handle(copy);check(bool(pending.deferred),"Copies release metadata workers while storage executes");
    auto progress=req("POST","/api/copy-progress?copy=copy-test-cancellation-12345");progress.headers["x-hls-token"]=token;
    auto initial=service.handle(progress);check(initial.status==200&&initial.body.find("\"done\":0")!=std::string::npos,"Copy reports real progress before storage starts");
    auto foreign=progress;foreign.peer_ip="192.0.2.11";check(service.handle(foreign).status==403,"Other peers cannot read copy progress");
    auto cancel=req("POST","/api/copy-cancel?copy=copy-test-cancellation-12345");cancel.headers["x-hls-token"]=token;
    auto other=cancel;other.peer_ip="192.0.2.11";check(service.handle(other).status==403,"Another peer cannot cancel someone else's copy");
    check(service.handle(cancel).status==200,"A client can cancel its own copy");
    std::mutex mutex;std::condition_variable ready;bool complete=false;unsigned status=0;
    pending.deferred([&](Response result){std::lock_guard<std::mutex> lock(mutex);status=result.status;complete=true;ready.notify_one();});
    {std::unique_lock<std::mutex> lock(mutex);check(ready.wait_for(lock,std::chrono::seconds(5),[&]{return complete;}),"Cancelled copy resolves its waiter");}
    check(status==499&&!std::filesystem::exists(root/"cancelled.txt"),"Cancelled copy cannot publish its private file");
}
void login_policy(){
    LoginAttempt attempt;auto now=LoginAttempt::Clock::now();
    for(unsigned round=1;round<=14;++round){
        for(unsigned i=1;i<5;++i)check(attempt.failed(now)==0,"Every retry round has exactly five chances");
        const auto expected=std::min(86400u,120u<<std::min(10u,round-1));
        check(attempt.failed(now)==expected,"Login penalty doubles from two minutes and caps at one day");
        const auto deadline=attempt.until;for(unsigned i=0;i<100;++i)check(attempt.failed(now)==expected&&attempt.until==deadline,"Blocked bursts are idempotent and cannot extend the deadline");
        now=deadline;check(attempt.wait(now)==0,"An expired ban permits the next round");
    }
}
void adaptive(){
    WorkerPool pool(1,16,3,1);std::mutex mutex;std::condition_variable ready;bool release=false;unsigned entered=0;
    for(int i=0;i<6;i++)check(pool.submit([&]{std::unique_lock<std::mutex> lock(mutex);++entered;ready.notify_all();ready.wait(lock,[&]{return release;});}),"Pool accepts bounded jobs");
    {std::unique_lock<std::mutex> lock(mutex);check(ready.wait_for(lock,std::chrono::seconds(3),[&]{return entered>=3;}),"Adaptive workers grow under demand");release=true;}ready.notify_all();
    for(int i=0;i<60 && pool.count()!=1;i++)std::this_thread::sleep_for(std::chrono::milliseconds(50));check(pool.count()==1,"Idle workers retire to minimum");
}
void upload_allowances(const std::filesystem::path& base){
    const auto root=base/"quota";std::filesystem::create_directory(root);WorkerPool workers(2,16);RuntimeStats stats;Config c;c.root=canonical_root(root);c.index=false;c.magika=false;c.write_mode="shared";c.ip_upload_bytes=10;c.ip_upload_overrides["192.0.2.11"]=0;Service service(c,workers,stats);
    const auto token=field(service.handle(req("GET","/api/session")).body,"token");auto first=req("PUT","/api/upload?path=/first");first.headers["x-hls-token"]=token;first.headers["content-length"]="8";auto a=service.begin_upload(first);
    auto second=first;second.target="/api/upload?path=/second";bool denied=false;try{service.begin_upload(second);}catch(const Failure& e){denied=e.status==413;}check(denied,"Known lengths reserve per-IP allowance across parallel uploads");
    a->append("1234",4);a.reset();second.headers["content-length"]="6";auto b=service.begin_upload(second);b->append("123456",6);check(service.finish_upload(second,b).status==201,"An aborted upload refunds only bytes that never arrived");b.reset();
    first.target="/api/upload?path=/unknown";first.headers.erase("content-length");a=service.begin_upload(first);denied=false;try{a->append("x",1);}catch(const Failure& e){denied=e.status==413;}check(denied,"Chunked uploads cannot bypass the cumulative IP allowance");a.reset();
    first.peer_ip="192.0.2.11";first.target="/api/upload?path=/unlimited-ip";a=service.begin_upload(first);a->append("unlimited-override",18);check(service.finish_upload(first,a).status==201,"A zero override makes one IP unlimited without changing the global limit");a.reset();
    auto host=req("POST","/api/admin/settings",true);host.headers["x-hls-token"]=token;host.body="{\"ipUploadOverrides\":{\"not-an-ip\":5}}";check(service.handle(host).status==400,"Per-IP override settings reject invalid addresses");host.body="{\"ipUploadOverrides\":{\"192.0.2.10\":0}}";check(service.handle(host).status==200,"Host may remove the allowance for one IP while service runs");
    first.peer_ip="192.0.2.10";first.target="/api/upload?path=/host-unlimited";a=service.begin_upload(first);a->append("again",5);check(service.finish_upload(first,a).status==201,"Dynamic zero override takes effect for the actual TCP peer");
    c.state_dir=root/".hls-shutdown-test-state";auto stopping=std::make_unique<Service>(c,workers,stats);first.target="/api/upload?path=/shutdown-pending";first.headers["x-hls-token"]=field(stopping->handle(req("GET","/api/session")).body,"token");first.headers["content-length"]="8";auto pending=stopping->begin_upload(first);pending->append("part",4);stopping.reset();pending.reset();check(!std::filesystem::exists(root/"shutdown-pending"),"Pending upload cleanup safely releases shared quota state after service shutdown");
}
void mount_permissions(const std::filesystem::path& base){
    namespace json=boost::json;const auto root=base/"mount-permissions";std::filesystem::create_directory(root);WorkerPool workers(1,16);RuntimeStats stats;Config c;c.root=canonical_root(root);c.index=false;c.magika=false;c.write_mode="readonly";Service service(c,workers,stats);
    const auto token=field(service.handle(req("GET","/api/session")).body,"token");
    auto call=[&](const std::string& endpoint,json::object data,bool local=false,std::string ip="192.0.2.10"){auto request=req("POST",endpoint,local,ip);request.headers["x-hls-token"]=token;request.body=json::serialize(data);return service.handle(request);};
    const json::object mount{{"path","/_mounts/192.0.2.10/Picked"},{"browser","page-permissions-test-12345"},{"writable",false},{"movable",false}};
    auto bad=mount;bad["path"]="/Hijack";check(call("/api/mount/register",bad).status==403,"Read-only remote mounts stay in a peer-specific virtual namespace");
    bad=mount;bad["writable"]=true;check(call("/api/mount/register",bad).status==403,"Read-only mode forbids writable browser mounts");
    check(call("/api/mount/native",json::object{{"path","/Host"},{"source",root.u8string()},{"writable",false}}).status==403,"Remote clients cannot turn arbitrary host paths into shares");
    const auto approved=call("/api/mount/register",mount);check(approved.status==200,"A remote read-only browser can publish a read-only directory");const auto key=field(approved.body,"key"),id=field(approved.body,"id");
    auto response=call("/api/mount/poll",json::object{{"providers",json::array{json::object{{"id",id},{"key",key}}}}},false,"192.0.2.11");check(response.status==403,"Provider keys remain bound to the original TCP peer");
    check(call("/api/mount/remove",json::object{{"path","/_mounts/192.0.2.10/Picked"},{"key","wrong"}}).status==403,"Another browser cannot unmount using only the public ID");
    auto settings=req("POST","/api/admin/settings",true);settings.headers["x-hls-token"]=token;settings.body="{\"writeMode\":\"isolated\"}";check(service.handle(settings).status==200,"Switch to isolated for browser mount authorization");
    bad=mount;bad["path"]="/_clients/192.0.2.11/Picked";check(call("/api/mount/register",bad).status==403,"Isolated mounts cannot be published inside another IP tree");
    bad["path"]="/_clients/192.0.2.10/Picked";check(call("/api/mount/register",bad).status==200&&!std::filesystem::exists(root/"_clients"),"Virtual IP mounts do not create empty physical owner directories");
}
void collaboration(const std::filesystem::path& base){
    namespace json=boost::json;const auto root=base/"collaboration";std::filesystem::create_directory(root);
    WorkerPool workers(2,32);RuntimeStats stats;Config c;c.root=canonical_root(root);c.index=false;c.magika=false;c.write_mode="isolated";Service service(c,workers,stats);
    const auto token=field(service.handle(req("GET","/api/session")).body,"token");
    auto call=[&](std::string method,std::string target,std::string body="",bool local=false,std::string ip="192.0.2.10"){
        auto r=req(method,target,local,ip);r.headers["x-hls-token"]=token;r.body=body;return service.handle(r);
    };
    const std::string own="/_clients/192.0.2.10",id="page-testing-browser-12345";
    check(call("GET","/api/list?path="+own+"&browser="+id).status==200&&!std::filesystem::exists(root/"_clients"),"An uncreated owner has a virtual empty listing");
    check(call("POST","/api/alias","{\"name\":\"before upload\"}").status==403,"Name submissions require a successful upload");
    auto upload=req("PUT","/api/upload?parents=1&path="+own+"/folder/nested/file.txt");upload.headers["x-hls-token"]=token;
    auto sink=service.begin_upload(upload);sink->append("first",5);check(service.finish_upload(upload,sink).status==201,"Folder upload creates safe parents lazily");sink.reset();
    check(call("POST","/api/alias","{\"name\":\"Alice <b>\"}").status==200,"Uploader may submit an untrusted name");
    auto visible=call("GET","/api/list?path="+own+"/folder/nested&browser="+id);check(visible.body.find("Alice")==std::string::npos,"Pending name is absent from public listings");
    check(call("GET","/api/site").body.find("Alice")==std::string::npos&&call("POST","/api/browse","{\"id\":\""+id+"\",\"paths\":[\""+own+"/folder/nested\"],\"current\":\""+own+"\"}").body.find("Alice")==std::string::npos,"Public settings and browser heartbeat cannot reveal pending names");
    check(call("GET","/api/admin/aliases").status==403,"A remote uploader cannot read the review queue");
    auto reviews=json::parse(call("GET","/api/admin/aliases","",true).body).as_object().at("entries").as_array();check(reviews.size()==1,"Host receives pending request");
    const auto revision=std::string(reviews[0].as_object().at("revision").as_string());
    auto decision=json::serialize(json::object{{"path",own},{"revision",revision},{"action","approve"}});
    check(call("POST","/api/admin/aliases",decision).status==403,"Remote peer cannot approve its own name");
    check(call("POST","/api/admin/aliases",decision,true).status==200,"Host can approve the exact pending revision");
    check(call("GET","/api/list?path="+own+"&browser="+id).body.find("Alice <b>")!=std::string::npos,"Only approved name is exposed as entry metadata");
    check(call("POST","/api/admin/aliases",decision,true).status==404,"Reviewed request cannot be replayed");
    check(call("POST","/api/alias","{\"name\":\"Bad\\nName\"}").status==400,"Names cannot contain control characters");
    auto baseline=json::parse(call("GET","/api/list?path="+own+"/folder/nested&browser="+id).body).as_object().at("stamp");
    std::ofstream(root/"_clients/192.0.2.10/folder/nested/file.txt",std::ios::trunc)<<"other";
    std::filesystem::last_write_time(root/"_clients/192.0.2.10/folder/nested/file.txt",std::filesystem::file_time_type::clock::now()+std::chrono::seconds(2));
    std::this_thread::sleep_for(std::chrono::milliseconds(5100));
    const auto state="{\"id\":\""+id+"\",\"paths\":[\""+own+"/folder/nested\"],\"current\":\""+own+"/folder/nested\"}";
    check(json::parse(call("POST","/api/browse",state).body).as_object().at("stamp")!=baseline,"Directory watcher detects a same-size external rewrite");
    check(call("DELETE","/api/entry?path="+own+"/folder/nested/file.txt").status==200,"Uploader may delete the final file");
    service.maintain();service.maintain();check(std::filesystem::exists(root/"_clients/192.0.2.10"),"Browsing a descendant pins the empty owner tree");
    check(call("POST","/api/browse","{\"id\":\""+id+"\",\"paths\":[],\"current\":\"/\"}").status==200,"Leaving releases every path for this browser");
    service.maintain();service.maintain();check(!std::filesystem::exists(root/"_clients/192.0.2.10"),"Empty IP folder is removed only after every browser leaves");
    upload.target="/api/upload?parents=1&path="+own+"/aborted/sub/file.txt";sink=service.begin_upload(upload);sink->append("partial",7);sink.reset();service.maintain();service.maintain();check(!std::filesystem::exists(root/"_clients/192.0.2.10"),"Aborted folder uploads leave no empty owner tree");
    for(const auto& unsafe:{"/_clients/192.0.2.10/a/../escape.txt","/_clients/192.0.2.11/escape.txt"}){
        upload.target="/api/upload?parents=1&path="+url_encode(unsafe);bool denied=false;try{service.begin_upload(upload);}catch(const std::exception&){denied=true;}check(denied,"Recursive uploads cannot traverse or write another IP tree");
    }
}
void attention(const std::filesystem::path& root){
    std::ofstream(root/"attention.txt")<<"host-only mark";
    WorkerPool workers(1,16);RuntimeStats stats;Config c;c.root=root;c.index=false;c.magika=false;Service service(c,workers,stats);
    const auto token=field(service.handle(req("GET","/api/session",true)).body,"token");
    auto request=req("POST","/api/admin/attention",false,"192.0.2.55");request.body="{\"path\":\"/attention.txt\",\"marked\":true}";request.headers["x-hls-token"]=token;
    request.headers["x-forwarded-for"]="127.0.0.1";
    check(service.handle(request).status==403,"Remote client cannot mark files even with CSRF token and spoofed forwarding header");
    check(service.settings()->attention_paths.empty(),"Denied remote mark has no side effects");
    request.local_admin=true;check(service.handle(request).status==200,"Host can mark in readonly mode");
    request.local_admin=false;request.body="{\"path\":\"/attention.txt\",\"marked\":false}";
    check(service.handle(request).status==403&&service.settings()->attention_paths.size()==1,"Remote client cannot unmark host attention");
    request.target="/api/admin/settings";request.body="{\"attentionClick\":\"download\"}";
    check(service.handle(request).status==403&&service.settings()->attention_click=="locate","Remote client cannot change the global attention action");
    check(service.handle(req("GET","/api/site",false,"192.0.2.55")).body.find("\"attentionClick\":\"locate\"")!=std::string::npos,"Remote clients receive the global attention action");
}
void cow_and_drops(const std::filesystem::path& root){
    WorkerPool workers(2,32);RuntimeStats stats;Config c;c.root=root;c.index=false;c.magika=false;c.write_mode="shared";Service service(c,workers,stats);
    std::filesystem::create_directories(root/"cow");std::ofstream(root/"cow/data.bin",std::ios::binary)<<"old-generation";
    const auto token=field(service.handle(req("GET","/api/session",true)).body,"token");
    auto request=req("PUT","/api/upload?path=/cow/data.bin&replaceVersion="+url_encode(File::open(root,"/cow/data.bin")->info().version),true);request.headers["x-hls-token"]=token;
    auto old=File::open(root,"/cow/data.bin");auto upload=service.begin_upload(request);const std::string replacement="new-generation";upload->append(replacement.data(),replacement.size());
    bool busy=false;try{service.begin_upload(request);}catch(const Failure& e){busy=e.status==409;}check(busy,"Concurrent upload to a reserved name is rejected");
    auto move=req("POST","/api/rename?path=/cow/data.bin&to=/cow/moved.bin",true);move.headers["x-hls-token"]=token;check(service.handle(move).status==409,"Move cannot race an active replacement");
    char bytes[14]{};old->read(0,bytes,14);check(std::string(bytes,14)=="old-generation","Readers see original bytes before publish");
    check(service.finish_upload(request,upload).status==200,"Versioned replacement publishes a new generation");upload.reset();
    old->read(0,bytes,14);check(std::string(bytes,14)=="old-generation"&&old->unchanged(),"Open old generation remains readable after COW replacement");
    auto fresh=File::open(root,"/cow/data.bin");fresh->read(0,bytes,14);check(std::string(bytes,14)==replacement&&fresh->info().version!=old->info().version,"New readers open the new generation");
    bool stale=false;try{service.begin_upload(request);}catch(const Failure& e){stale=e.status==409;}check(stale,"Stale overwrite version cannot replace newer content");
    move.target="/api/rename?path=/cow/data.bin&to=/cow/moved.bin&version="+url_encode(old->info().version);check(service.handle(move).status==409,"Stale file move version is rejected");
    move.target="/api/rename?path=/cow/data.bin&to=/cow/moved.bin&version="+url_encode(fresh->info().version);check(service.handle(move).status==200,"Writable file moves to another name");
    fresh->read(0,bytes,14);check(std::string(bytes,14)==replacement,"Moving a file does not break its open reader");
    c.write_mode="isolated";c.state_dir=root/".hls-isolated-test-state";Service isolated(c,workers,stats);const auto isolated_token=field(isolated.handle(req("GET","/api/session",false,"198.51.100.7")).body,"token");
    auto drop=req("PUT","/api/upload?path=/any-folder/drop.txt&drop=1",false,"198.51.100.7");drop.headers["x-hls-token"]=isolated_token;auto sink=isolated.begin_upload(drop);sink->append("safe",4);
    check(sink->path()=="/_clients/198.51.100.7/drop.txt"&&isolated.finish_upload(drop,sink).status==201,"Isolated external drop maps into actual peer IP directory");sink.reset();
    std::filesystem::create_directory(root/"_clients/198.51.100.7/inner");drop.target="/api/upload?path=/_clients/198.51.100.7/inner/file.txt&drop=1";sink=isolated.begin_upload(drop);check(sink->path()=="/_clients/198.51.100.7/inner/file.txt","Drop retains a folder already inside the IP root");sink.reset();
    auto host=isolated.handle(req("GET","/api/session",true,"127.0.0.1"));check(host.body.find("\"uploadRoot\":\"/\"")!=std::string::npos&&host.body.find("\"writableRoot\":\"/\"")!=std::string::npos,"Host administrator upload root is unrestricted");
    drop.local_admin=true;drop.peer_ip="127.0.0.1";drop.target="/api/upload?path=/host-drop.txt&drop=1";sink=isolated.begin_upload(drop);check(sink->path()=="/host-drop.txt","Host external drops retain their chosen shared path");sink.reset();
    drop.local_admin=false;drop.peer_ip="198.51.100.7";drop.target="/api/upload?path=/../escape&drop=1";bool traversal=false;try{isolated.begin_upload(drop);}catch(const Failure& e){traversal=e.status==400;}catch(const std::invalid_argument&){traversal=true;}check(traversal,"Drop routing never sanitizes away a traversal attack");
}
void features(const std::filesystem::path& root){
    auto extract=[&](const std::string& bytes){
        {std::ofstream output(root/"features",std::ios::binary|std::ios::trunc);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
        return extract_magika_features(File::open(root,"/features"));
    };
    // The official model pads the beginning on the right and the end on the left,
    // stripping whitespace independently within each bounded 4096-byte window.
    auto expected=magika::Features{};expected.fill(256);
    check(extract("")==expected,"Empty feature windows use padding token 256");
    expected[0]='A';expected[1]='B';expected[2]='\n';expected[3]='\r';
    expected[2044]=' ';expected[2045]='\t';expected[2046]='A';expected[2047]='B';
    check(extract(" \tAB\n\r")==expected,"Independent trimming and opposite padding directions match Magika");
    const auto binary=extract(std::string("\0\xff\x80\x01",4));
    check(binary[0]==0&&binary[1]==255&&binary[2]==128&&binary[2044]==0&&binary[2047]==1,"Binary features preserve unsigned byte values");
    std::string long_file(12000,'x');
    for(std::size_t i=20;i<long_file.size()-20;++i)long_file[i]=static_cast<char>('A'+i%26);
    long_file.replace(0,20,20,' ');long_file.replace(long_file.size()-20,20,20,'\n');
    const auto large=extract(long_file);
    for(std::size_t i=0;i<1024;++i){check(large[i]==long_file[20+i],"Beginning reads first window");check(large[1024+i]==long_file[long_file.size()-20-1024+i],"Ending reads last window");}
    expected.fill(256);check(extract(std::string(5000,' ')+"middle"+std::string(5000,'\n'))==expected,"Whitespace reads remain bounded to each 4096-byte window");
}
}
int main(){const auto root=std::filesystem::temp_directory_path()/("hls-write-"+random_token());std::filesystem::create_directories(root);try{copy_cancellation(canonical_root(root));login_policy();writes(canonical_root(root));attention(canonical_root(root));cow_and_drops(canonical_root(root));collaboration(canonical_root(root));upload_allowances(canonical_root(root));mount_permissions(canonical_root(root));adaptive();features(canonical_root(root));std::filesystem::remove_all(root);std::cout<<"Write permissions, lazy owners, folder uploads, name moderation, directory watches, COW and adaptive pool tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(root);return 1;}}
