// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "sha256.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <algorithm>
#include <set>

namespace hls {
namespace json=boost::json;
namespace {
constexpr std::uint64_t piece_bytes=1ull<<20;
constexpr std::uint32_t page_pieces=16;
Response encode(json::object object){Response r;r.body=json::serialize(object);r.headers={{"Cache-Control","no-store"}};return r;}
std::string field(const json::object& data,const char* name,std::size_t max){const auto* v=data.if_contains(name);if(!v||!v->is_string()||v->as_string().size()>max)throw Failure(400,std::string("Invalid P2P ")+name);return std::string(v->as_string());}
std::uint64_t number(const json::object& data,const char* name,std::uint64_t max,std::uint64_t fallback=0){const auto* v=data.if_contains(name);if(!v)return fallback;if((!v->is_uint64()&&!v->is_int64())||(v->is_int64()&&v->as_int64()<0))throw Failure(400,"Invalid P2P number");const auto n=v->is_uint64()?v->as_uint64():static_cast<std::uint64_t>(v->as_int64());if(n>max)throw Failure(400,"P2P number exceeds limit");return n;}
std::string cookie(const Request& request){const auto text=request.header("cookie");std::size_t at=0;while(at<text.size()){while(at<text.size()&&(text[at]==' '||text[at]==';'))++at;const auto end=text.find(';',at);const auto item=text.substr(at,end==std::string::npos?end:end-at);if(item.rfind("HLS_SID=",0)==0)return item.substr(8);if(end==std::string::npos)break;at=end+1;}return {};}
std::size_t weight(const P2PState::Message& message){return message.sdp.size()+message.from.size()+128;}
json::object metadata(const P2PState::Peer& peer,const Config& config,const LiveSettings& policy){return {{"id",peer.id},{"key",peer.key},{"path",peer.path},{"version",peer.version},{"size",peer.size},{"pieceBytes",piece_bytes},{"pieces",(peer.size+piece_bytes-1)/piece_bytes},{"url","/download?path="+url_encode(peer.path)+"&version="+url_encode(peer.version)},{"maxPeers",config.p2p_peers},{"cacheMiB",policy.p2p_cache_mb},{"cacheSeconds",policy.p2p_cache_seconds},{"uploadKiB",config.p2p_upload_kbps},{"sourceMount",peer.source_mount},{"sourceVersion",peer.source_version},{"sourcePath",peer.source_path},{"source",peer.provider}};}
}
void P2PState::prune(std::uint64_t revision){
    const auto now=Clock::now();
    for(auto i=relays.begin();i!=relays.end();)if(i->second.expires<=now||i->second.revision!=revision){for(const auto& id:i->second.assigned){auto peer=peers.find(id);if(peer!=peers.end()){for(const auto& message:peer->second.messages)signal_bytes-=weight(message);peers.erase(peer);}}i=relays.erase(i);}else ++i;
    for(auto i=peers.begin();i!=peers.end();)if(i->second.expires<=now||i->second.revision!=revision){for(const auto& message:i->second.messages)signal_bytes-=weight(message);i=peers.erase(i);}else ++i;
    for(auto i=hashes.begin();i!=hashes.end();)if(now-i->second.used>std::chrono::minutes(5)){hash_bytes-=i->first.size()+i->second.body.size()+128;i=hashes.erase(i);}else ++i;
}
void P2PState::assign_relays(std::uint64_t revision){
    // Source pages are preferred over pull-through caches. One source page may
    // serve four active files; generic idle pages retain a one-file budget.
    if(peers.size()>=512)return;
    struct Demand {const Peer* source=nullptr;std::size_t members=0;bool assigned=false,provider=false;};
    std::map<std::pair<std::string,std::string>,Demand> files;std::map<std::string,std::size_t> ips;
    for(const auto& item:peers){const auto& p=item.second;auto& f=files[{p.path,p.version}];++f.members;++ips[p.ip];if(p.relay)f.assigned=true;if(p.provider)f.provider=true;if(!p.relay&&p.downloading)f.source=&p;}
    for(auto& page:relays){auto& r=page.second;r.assigned.erase(std::remove_if(r.assigned.begin(),r.assigned.end(),[&](const auto& id){return !peers.count(id);}),r.assigned.end());}
    for(unsigned pass=0;pass<2;++pass)for(auto& page:relays){auto& relay=page.second;
        for(auto& item:files){auto& f=item.second;if(peers.size()>=512)return;if(!f.source||f.members>=128||ips[relay.ip]>=32)continue;
            const auto* d=f.source;const bool owns=!d->source_mount.empty()&&std::find(relay.sources.begin(),relay.sources.end(),d->source_mount)!=relay.sources.end();
            if((pass==0&&!owns)||(pass==1&&owns)||(owns?f.provider:f.assigned)||relay.assigned.size()>=(owns?4u:1u))continue;
            Peer p;p.id=random_token();p.key=random_token();p.ip=relay.ip;p.cookie=relay.cookie;p.path=d->path;p.version=d->version;p.size=d->size;p.source_mount=d->source_mount;p.source_version=d->source_version;p.source_path=d->source_path;p.provider=owns;p.revision=revision;p.relay=true;p.downloading=false;p.expires=Clock::now()+std::chrono::seconds(20);p.rate_start=Clock::now();
            relay.assigned.push_back(p.id);peers.emplace(p.id,std::move(p));f.assigned=true;f.provider=f.provider||owns;++f.members;++ips[relay.ip];
        }
    }
}
Response Service::p2p_join(const Request& request,const std::string& path,const std::string& version,std::uint64_t length,const std::string& source_mount,const std::string& source_version,const std::string& source_path){
    const auto policy=settings();if(!policy->p2p||!access_guard(request,true,false))throw Failure(403,"P2P policy changed");protect_config(path);script_file(request,"read",path,length);
    const auto count=(length+piece_bytes-1)/piece_bytes;if(!length||count>1048576)throw Failure(413,"P2P supports non-empty files up to 1 TiB");
    std::lock_guard<std::mutex> lock(p2p_.mutex);p2p_.prune(policy->access_revision);
    std::size_t same_ip=0,same_file=0;for(const auto& item:p2p_.peers){if(item.second.ip==request.peer_ip)++same_ip;if(item.second.path==path&&item.second.version==version)++same_file;}
    if(p2p_.peers.size()>=512||same_ip>=32||same_file>=128)throw Failure(503,"P2P participant budget reached");
    P2PState::Peer peer;peer.id=random_token();peer.key=random_token();peer.ip=request.peer_ip;peer.cookie=cookie(request);peer.path=path;peer.version=version;peer.size=length;peer.source_mount=source_mount;peer.source_version=source_version;peer.source_path=source_path;peer.revision=policy->access_revision;peer.expires=P2PState::Clock::now()+std::chrono::seconds(20);peer.rate_start=P2PState::Clock::now();
    auto response=encode(metadata(peer,config_,*policy));p2p_.peers.emplace(peer.id,std::move(peer));if(policy->p2p_cache_mb)p2p_.assign_relays(policy->access_revision);return response;
}
std::shared_ptr<File> Service::p2p_file(const P2PState::Peer& peer)const{
    protect_config(peer.path);
    if(peer.source_mount.empty()){auto file=open_file(peer.path);if(file->info().version!=peer.version||!file->unchanged())throw Failure(409,"File changed; stop the old peer swarm");return file;}
    Mount mount;if(!remote_mount(peer.path,&mount)||mount.id!=peer.source_mount)throw Failure(409,"Mounted source generation changed");
    const auto replica=mount.mirrored.find(peer.path);
    if(replica!=mount.mirrored.end()&&replica->second.source==peer.source_version){try{auto file=open_file(peer.path);if(file->info().version==replica->second.host&&file->info().size==peer.size)return file;}catch(const Failure& e){if(e.status!=404)throw;}}
    if(mount.expires<P2PState::Clock::now())throw Failure(503,"Source is offline and this file has no complete current replica");return {};
}
Response Service::p2p_api(const Request& request,const std::string& route){
    const auto policy=settings();
    if(!policy->p2p)throw Failure(404,"P2P downloads are disabled");
    browser_guard(request,true);
    if(request.method!="POST")throw Failure(405,"P2P control requires POST");
    if(!request.preview_key.empty())throw Failure(403,"Preview grants cannot enter a peer swarm");
    if(request.body.size()>49152)throw Failure(413,"P2P control body exceeds limit");
    boost::system::error_code ec;json::parse_options options;options.max_depth=5;const auto document=json::parse(request.body,ec,{},options);
    if(ec||!document.is_object())throw Failure(400,"Expected P2P object");const auto& data=document.as_object();
    // Polling alone never renews hotlink access. Only a newly reported transfer
    // may extend it, and only while the current access session is still valid.
    if(!access_guard(request,true,route=="/api/p2p/join"||route=="/api/p2p/hashes"))throw Failure(401,"Login required");
    if(route=="/api/p2p/relay-join"){
        std::lock_guard<std::mutex> lock(p2p_.mutex);p2p_.prune(policy->access_revision);
        std::size_t same_ip=0;for(const auto& item:p2p_.relays)if(item.second.ip==request.peer_ip)++same_ip;
        if(p2p_.relays.size()>=512||same_ip>=32)throw Failure(503,"PCDN page budget reached");
        P2PState::Relay relay;relay.id=random_token();relay.key=random_token();relay.ip=request.peer_ip;relay.cookie=cookie(request);relay.revision=policy->access_revision;relay.expires=P2PState::Clock::now()+std::chrono::seconds(20);
        auto response=encode({{"id",relay.id},{"key",relay.key}});p2p_.relays.emplace(relay.id,std::move(relay));return response;
    }
    if(route=="/api/p2p/relay-poll"||route=="/api/p2p/relay-leave"){
        const auto id=field(data,"id",48),key=field(data,"key",48);
        std::vector<std::string> source_ids;
        if(const auto* sources=data.if_contains("sources")){
            if(!sources->is_array()||sources->as_array().size()>8)throw Failure(400,"Invalid mounted source proofs");
            std::lock_guard<std::mutex> mounts(mount_mutex_);
            for(const auto& source:sources->as_array()){
                if(!source.is_object())throw Failure(400,"Invalid source proof");const auto mid=field(source.as_object(),"id",48),secret=field(source.as_object(),"key",48);bool valid=false;
                for(const auto& item:mounts_)if(item.second.id==mid&&item.second.remote&&item.second.key==secret&&item.second.peer==request.peer_ip&&item.second.expires>P2PState::Clock::now()){valid=true;break;}
                if(!valid)throw Failure(403,"Mounted source proof is invalid");source_ids.push_back(mid);
            }
        }
        std::lock_guard<std::mutex> lock(p2p_.mutex);p2p_.prune(policy->access_revision);auto found=p2p_.relays.find(id);
        if(found==p2p_.relays.end()||found->second.key!=key||found->second.ip!=request.peer_ip||found->second.cookie!=cookie(request))throw Failure(403,"PCDN page lease expired or belongs to another browser");
        auto& relay=found->second;
        auto revoke=[&](const std::string& assigned){auto peer=p2p_.peers.find(assigned);if(peer!=p2p_.peers.end()){for(const auto& message:peer->second.messages)p2p_.signal_bytes-=weight(message);p2p_.peers.erase(peer);}};
        if(route=="/api/p2p/relay-leave"){for(const auto& assigned_id:relay.assigned)revoke(assigned_id);p2p_.relays.erase(found);return encode({{"ok",true}});}
        relay.sources=std::move(source_ids);relay.expires=P2PState::Clock::now()+std::chrono::seconds(20);
        for(auto it=relay.assigned.begin();it!=relay.assigned.end();){
            const auto previous=p2p_.peers.find(*it);bool demand=false,other=false;
            for(const auto& p:p2p_.peers)if(!p.second.relay&&p.second.downloading){if(previous!=p2p_.peers.end()&&p.second.path==previous->second.path&&p.second.version==previous->second.version)demand=true;else other=true;}
            const bool lost_source=previous!=p2p_.peers.end()&&previous->second.provider&&std::find(relay.sources.begin(),relay.sources.end(),previous->second.source_mount)==relay.sources.end();
            if(previous==p2p_.peers.end()||lost_source||(!demand&&other)){revoke(*it);it=relay.assigned.erase(it);}else ++it;
        }
        if(policy->p2p_cache_mb)p2p_.assign_relays(policy->access_revision);
        json::array assignments;std::vector<std::string> allowed;for(const auto& assigned_id:relay.assigned){const auto peer=p2p_.peers.find(assigned_id);if(peer==p2p_.peers.end())continue;try{script_file(request,"read",peer->second.path,peer->second.size);assignments.emplace_back(metadata(peer->second,config_,*policy));allowed.push_back(assigned_id);}catch(const Failure& error){if(error.status!=403)throw;revoke(assigned_id);}}relay.assigned=std::move(allowed);
        json::value first=assignments.empty()?json::value(nullptr):assignments.front();return encode({{"assignment",std::move(first)},{"assignments",std::move(assignments)}});
    }
    if(route=="/api/p2p/join"){
        const auto path=normalize_path(field(data,"path",4096)),version=field(data,"version",512);
        protect_config(path);Mount mount;
        if(version.rfind("mount:",0)==0){
            if(!remote_mount(path,&mount))throw Failure(409,"Mounted source was removed");
            const auto relative=path.substr(mount.path.size());
            return remote_call(mount.path,json::serialize(json::object{{"op","stat"},{"path",relative}}),"",[this,request,path,version,mount,relative](Response result){
                if(result.status>=400)return result;const auto object=json::parse(result.body).as_object();const auto source=field(object,"version",512);if(version!="mount:"+source)throw Failure(409,"Mounted file changed before peer download");
                const auto length=number(object,"size",1ull<<40);return p2p_join(request,path,version,length,mount.id,source,relative);
            });
        }
        const auto file=open_file(path);if(file->info().version!=version)throw Failure(409,"File changed before peer download");return p2p_join(request,path,version,file->info().size);
    }
    const auto id=field(data,"id",48),key=field(data,"key",48);
    P2PState::Peer snapshot;
    {
        std::lock_guard<std::mutex> lock(p2p_.mutex);p2p_.prune(policy->access_revision);const auto found=p2p_.peers.find(id);
        if(found==p2p_.peers.end()||found->second.key!=key||found->second.ip!=request.peer_ip||found->second.cookie!=cookie(request))throw Failure(403,"Peer lease expired or belongs to another browser");
        const auto& p=found->second;snapshot.path=p.path;snapshot.version=p.version;snapshot.size=p.size;snapshot.source_mount=p.source_mount;snapshot.source_version=p.source_version;snapshot.source_path=p.source_path;
    }
    if(route=="/api/p2p/leave"){
        std::lock_guard<std::mutex> lock(p2p_.mutex);const auto found=p2p_.peers.find(id);if(found!=p2p_.peers.end()){for(const auto& m:found->second.messages)p2p_.signal_bytes-=weight(m);p2p_.peers.erase(found);}return encode({{"ok",true}});
    }
    script_file(request,"read",snapshot.path,snapshot.size);const auto file=p2p_file(snapshot);
    if(route=="/api/p2p/hashes"){
        const auto start=static_cast<std::uint32_t>(number(data,"start",1048576));const auto count=(snapshot.size+piece_bytes-1)/piece_bytes;
        if(start%page_pieces||start>=count)throw Failure(400,"Hash page must be aligned and within the file");
        const auto cache_key=snapshot.path+"\n"+snapshot.version+":"+std::to_string(start);Response response;
        {std::lock_guard<std::mutex> lock(p2p_.mutex);auto found=p2p_.hashes.find(cache_key);if(found!=p2p_.hashes.end()){found->second.used=P2PState::Clock::now();response.body=found->second.body;response.headers={{"Cache-Control","no-store"}};return response;}
            if(p2p_.pending.size()>=4&&!p2p_.pending.count(cache_key))throw Failure(503,"P2P hash workers are busy");
        }
        if(!file){
            Mount mount;if(!remote_mount(snapshot.path,&mount)||mount.id!=snapshot.source_mount)throw Failure(409,"Mounted source changed");
            auto remote=remote_call(mount.path,json::serialize(json::object{{"op","p2p-hashes"},{"path",snapshot.source_path},{"version",snapshot.source_version},{"start",start}}),"",[this,snapshot,request,start,count,cache_key,revision=policy->access_revision](Response result){
                if(result.status>=400)return result;
                if(!settings()->p2p||settings()->access_revision!=revision||!access_guard(request,true,false))throw Failure(403,"P2P policy changed");p2p_file(snapshot);
                const auto value=json::parse(result.body).as_object();const auto* hashes=value.if_contains("hashes");if(number(value,"start",1048576)!=start||!hashes||!hashes->is_array()||hashes->as_array().size()!=std::min<std::uint64_t>(16,count-start))throw Failure(502,"Invalid source manifest");
                for(const auto& hash:hashes->as_array())if(!hash.is_string()||hash.as_string().size()!=64||std::string(hash.as_string()).find_first_not_of("0123456789abcdef")!=std::string::npos)throw Failure(502,"Invalid source digest");
                result=encode({{"start",start},{"hashes",*hashes}});
                return result;
            });
            response.deferred=[this,remote,cache_key](std::function<void(Response)> done)mutable{
                {std::lock_guard<std::mutex> lock(p2p_.mutex);const auto cached=p2p_.hashes.find(cache_key);if(cached!=p2p_.hashes.end()){Response r;r.body=cached->second.body;r.headers={{"Cache-Control","no-store"}};done(std::move(r));return;}auto pending=p2p_.pending.find(cache_key);
                    if(pending!=p2p_.pending.end()){if(pending->second.size()>=32){done(error(503,"Too many source manifest waiters"));return;}pending->second.push_back(std::move(done));return;}
                    if(p2p_.pending.size()>=4){done(error(503,"Source manifest workers are busy"));return;}p2p_.pending[cache_key].push_back(std::move(done));
                }
                remote.deferred([this,cache_key](Response result){
                    std::vector<std::function<void(Response)>> callbacks;
                    {std::lock_guard<std::mutex> lock(p2p_.mutex);auto pending=p2p_.pending.find(cache_key);if(pending==p2p_.pending.end())return;callbacks=std::move(pending->second);p2p_.pending.erase(pending);
                        if(result.status==200){
                            while(!p2p_.hashes.empty()&&(p2p_.hash_bytes+cache_key.size()+result.body.size()+128>(8ull<<20)||p2p_.hashes.size()>=4096)){auto victim=std::min_element(p2p_.hashes.begin(),p2p_.hashes.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});p2p_.hash_bytes-=victim->first.size()+victim->second.body.size()+128;p2p_.hashes.erase(victim);}
                            auto existing=p2p_.hashes.find(cache_key);if(existing==p2p_.hashes.end()){p2p_.hash_bytes+=cache_key.size()+result.body.size()+128;p2p_.hashes[cache_key]={result.body,P2PState::Clock::now()};}else if(existing->second.body!=result.body)result=error(409,"Source manifest changed within one version");
                        }
                    }
                    for(auto& callback:callbacks)callback(result);
                });
            };return response;
        }
        response.deferred=[this,request,file,start,count,cache_key,path=snapshot.path,revision=policy->access_revision,snapshot](std::function<void(Response)> done){
            {std::lock_guard<std::mutex> lock(p2p_.mutex);const auto cached=p2p_.hashes.find(cache_key);if(cached!=p2p_.hashes.end()){Response r;r.body=cached->second.body;r.headers={{"Cache-Control","no-store"}};done(std::move(r));return;}auto found=p2p_.pending.find(cache_key);if(found!=p2p_.pending.end()){if(found->second.size()>=32){done(error(503,"Too many hash waiters"));return;}found->second.push_back(std::move(done));return;}
                if(p2p_.pending.size()>=4){done(error(503,"P2P hash workers are busy"));return;}p2p_.pending[cache_key].push_back(std::move(done));
            }
            auto complete=[this,cache_key](Response result){
                std::vector<std::function<void(Response)>> callbacks;
                {std::lock_guard<std::mutex> lock(p2p_.mutex);const auto found=p2p_.pending.find(cache_key);if(found==p2p_.pending.end())return;callbacks=std::move(found->second);p2p_.pending.erase(found);
                    if(result.status==200){while(!p2p_.hashes.empty()&&(p2p_.hash_bytes+cache_key.size()+result.body.size()+128>(8ull<<20)||p2p_.hashes.size()>=4096)){auto victim=std::min_element(p2p_.hashes.begin(),p2p_.hashes.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});p2p_.hash_bytes-=victim->first.size()+victim->second.body.size()+128;p2p_.hashes.erase(victim);}p2p_.hash_bytes+=cache_key.size()+result.body.size()+128;p2p_.hashes[cache_key]={result.body,P2PState::Clock::now()};}
                }
                for(auto& callback:callbacks)callback(result);
            };
            if(!copy_workers_.submit([this,request,file,start,count,path,revision,complete,snapshot]{Response result;try{
                json::array digests;std::vector<unsigned char> buffer(static_cast<std::size_t>(piece_bytes));
                for(std::uint64_t i=start;i<std::min<std::uint64_t>(count,start+page_pieces);++i){const auto offset=i*piece_bytes,n=static_cast<std::size_t>(std::min<std::uint64_t>(piece_bytes,file->info().size-offset));if(file->read(offset,buffer.data(),n)!=n)throw Failure(409,"File changed while hashing");Sha256 hash;hash.update(buffer.data(),n);digests.emplace_back(hash.finish());}
                if(!settings()->p2p||settings()->access_revision!=revision||!access_guard(request,true,false)||!file->unchanged()||!p2p_file(snapshot)||p2p_file(snapshot)->info().version!=file->info().version)throw Failure(409,"P2P policy or file changed while hashing");
                result=encode({{"start",start},{"hashes",std::move(digests)}});
            }catch(const Failure& e){result=error(e.status,e.what());}catch(...){result=error(500,"Cannot hash shared file");}complete(std::move(result));})){complete(error(503,"P2P hash queue is full"));}
        };return response;
    }
    bool traffic=false;Response result;
    {
        std::lock_guard<std::mutex> lock(p2p_.mutex);p2p_.prune(settings()->access_revision);auto found=p2p_.peers.find(id);if(found==p2p_.peers.end())throw Failure(403,"Peer lease expired");auto& peer=found->second;
        if(route=="/api/p2p/signal"){
            const auto to=field(data,"to",48),type=field(data,"type",8),sdp=field(data,"sdp",32768);if((type!="offer"&&type!="answer")||sdp.empty()||!valid_utf8(sdp)||sdp.find('\0')!=std::string::npos)throw Failure(400,"Invalid WebRTC signal");
            auto target=p2p_.peers.find(to);if(to==id||target==p2p_.peers.end()||target->second.path!=peer.path||target->second.version!=peer.version)throw Failure(403,"Signaling is restricted to this file version");
            const auto now=P2PState::Clock::now();if(now-peer.rate_start>=std::chrono::minutes(1)){peer.rate_start=now;peer.messages_sent=0;}
            if(peer.messages_sent>=60||target->second.messages.size()>=16||p2p_.signal_bytes+sdp.size()+176>(4ull<<20))throw Failure(429,"P2P signal budget reached");
            if(type=="offer"){peer.activity=now;target->second.activity=now;}
            P2PState::Message message{++target->second.sequence,id,type,sdp};p2p_.signal_bytes+=weight(message);target->second.messages.push_back(std::move(message));++peer.messages_sent;return encode({{"ok",true}});
        }
        if(route!="/api/p2p/poll")throw Failure(404,"Unknown P2P endpoint");
        const auto ack=number(data,"ack",peer.sequence),received=number(data,"received",1ull<<52),sent=number(data,"sent",1ull<<52);if(ack<peer.ack||received<peer.received||sent<peer.sent)throw Failure(400,"P2P counters cannot go backwards");
        peer.ack=ack;while(!peer.messages.empty()&&peer.messages.front().sequence<=ack){p2p_.signal_bytes-=weight(peer.messages.front());peer.messages.pop_front();}
        const auto* have=data.if_contains("have");if(!have||!have->is_array()||have->as_array().size()>256)throw Failure(400,"P2P availability exceeds bound");
        std::vector<std::pair<std::uint32_t,std::uint32_t>> ranges;std::uint64_t end=0;const auto pieces=(peer.size+piece_bytes-1)/piece_bytes;
        for(const auto& range:have->as_array()){if(!range.is_array()||range.as_array().size()!=2)throw Failure(400,"Invalid P2P range");json::object r{{"a",range.as_array()[0]},{"b",range.as_array()[1]}};const auto a=number(r,"a",pieces),b=number(r,"b",pieces);if(a>=b||a<end)throw Failure(400,"P2P ranges must be sorted and disjoint");ranges.emplace_back(static_cast<std::uint32_t>(a),static_cast<std::uint32_t>(b));end=b;}
        if(const auto* wanted=data.if_contains("wanted")){if(wanted->is_null())peer.downloading=false;else{json::object w{{"index",*wanted}};const auto index=number(w,"index",pieces);if(index>=pieces)throw Failure(400,"Wanted piece exceeds file");if(!peer.relay&&(!peer.downloading||peer.wanted!=index))peer.activity=P2PState::Clock::now();peer.wanted=static_cast<std::uint32_t>(index);peer.downloading=!peer.relay;}}
        traffic=received>peer.received||sent>peer.sent;p2p_.reported_peer_bytes.fetch_add(received-peer.received);peer.received=received;peer.sent=sent;peer.have=std::move(ranges);peer.expires=P2PState::Clock::now()+std::chrono::seconds(20);
        if(traffic)peer.activity=P2PState::Clock::now();
        auto confirmed=peer.activity;for(const auto& item:p2p_.peers)if(item.second.path==peer.path&&item.second.version==peer.version)confirmed=std::max(confirmed,item.second.activity);
        json::array peers,messages;
        std::vector<const P2PState::Peer*> candidates;
        for(const auto& item:p2p_.peers){const auto& other=item.second;if(other.id!=id&&other.path==peer.path&&other.version==peer.version)candidates.push_back(&other);}
        if(!candidates.empty()){const auto offset=peer.discovery++%candidates.size();std::rotate(candidates.begin(),candidates.begin()+offset,candidates.end());}
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto* a,const auto* b){const auto rank=[](const auto* peer){return peer->provider?3:!peer->have.empty()?2:peer->relay?1:0;};return rank(a)>rank(b);});
        for(const auto* other:candidates){if(peers.size()>=config_.p2p_peers*2)break;json::array available;for(const auto& range:other->have)available.emplace_back(json::array{range.first,range.second});peers.emplace_back(json::object{{"id",other->id},{"have",std::move(available)},{"relay",other->relay},{"source",other->provider}});}
        for(const auto& message:peer.messages)messages.emplace_back(json::object{{"sequence",message.sequence},{"from",message.from},{"type",message.type},{"sdp",message.sdp}});
        const auto live=settings();result=encode({{"peers",std::move(peers)},{"messages",std::move(messages)},{"leaseSeconds",20},{"cacheMiB",live->p2p_cache_mb},{"cacheSeconds",live->p2p_cache_seconds},{"activityAgeMs",std::chrono::duration_cast<std::chrono::milliseconds>(P2PState::Clock::now()-confirmed).count()}});
    }
    if(traffic)transfer_activity(request);return result;
}
}
