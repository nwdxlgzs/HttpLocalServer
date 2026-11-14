// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <boost/asio/ip/address.hpp>
#include <algorithm>

namespace hls {
namespace json=boost::json;
namespace {
json::object published(const LiveSettings& value){
    json::array paths;for(const auto& path:value.attention_paths)paths.emplace_back(path);
    return {{"title",value.title},{"announcement",value.announcement},{"attentionPaths",std::move(paths)},{"attentionClick",value.attention_click},{"previewAllowed",value.preview},{"thumbnailsEnabled",value.thumbnails},{"p2pEnabled",value.p2p},{"p2pCacheMiB",value.p2p_cache_mb},{"p2pCacheSeconds",value.p2p_cache_seconds},{"revision",value.revision}};
}
Response encode(json::object value){Response result;result.body=json::serialize(value);result.headers={{"Cache-Control","no-store"}};return result;}
}
Response Service::site_settings(){auto value=published(*settings());value["zipPreview"]=settings()->zip_preview;return encode(std::move(value));}
Response Service::admin_settings(const Request& request){
    if(!request.local_admin)throw Failure(403,"Host settings can only be unlocked on this machine");
    browser_guard(request,request.method!="GET");
    if(request.method!="GET"&&request.method!="POST")throw Failure(405,"Use GET or POST for host settings");
    if(request.method=="POST"){
        if(request.body.size()>65536)throw Failure(413,"Settings document is too large");
        boost::system::error_code ec;json::parse_options options;options.max_depth=8;
        const auto document=json::parse(request.body,ec,{},options);
        if(ec||!document.is_object())throw Failure(400,"Expected a JSON settings object");
        std::lock_guard<std::mutex> lock(write_mutex_);
        auto next=std::make_shared<LiveSettings>(*settings());
        for(const auto& member:document.as_object()){
            const auto key=std::string(member.key());const auto& value=member.value();
            auto string=[&](std::size_t limit){if(!value.is_string()||value.as_string().size()>limit)throw Failure(400,"Invalid text for "+key);auto text=std::string(value.as_string());if(!valid_utf8(text))throw Failure(400,"Settings must be UTF-8");return text;};
            auto boolean=[&](){if(!value.is_bool())throw Failure(400,"Expected boolean for "+key);return value.as_bool();};
            auto number=[&](std::uint64_t low,std::uint64_t high){if(!value.is_int64()&&!value.is_uint64())throw Failure(400,"Expected integer for "+key);if(value.is_int64()&&value.as_int64()<0)throw Failure(400,"Negative limit");const auto n=value.is_uint64()?value.as_uint64():static_cast<std::uint64_t>(value.as_int64());if(n<low||n>high)throw Failure(400,"Limit out of range for "+key);return n;};
            if(key=="recycleEnabled")next->recycle=boolean();
            else if(key=="recycleDays")next->recycle_days=static_cast<unsigned>(number(1,3650));
            else if(key=="recycleMiB")next->recycle_bytes=number(1,1073741824)<<20;
            else if(key=="ipTransfers")next->ip_transfers=static_cast<unsigned>(number(0,4096));
            else if(key=="uploadKiB")next->upload_kbps=number(0,1073741824);
            else if(key=="downloadKiB")next->download_kbps=number(0,1073741824);
            else if(key=="ipUploadKiB")next->ip_upload_kbps=number(0,1073741824);
            else if(key=="ipDownloadKiB")next->ip_download_kbps=number(0,1073741824);
            else if(key=="loginPassword")next->login_password=string(1024);
            else if(key=="hotlinkSeconds")next->hotlink_seconds=static_cast<unsigned>(number(0,86400));
            else if(key=="writeMode"){next->write_mode=string(16);if(next->write_mode!="readonly"&&next->write_mode!="isolated"&&next->write_mode!="shared")throw Failure(400,"Unknown write mode");}
            else if(key=="attentionClick"){next->attention_click=string(16);if(next->attention_click!="locate"&&next->attention_click!="download")throw Failure(400,"Attention click must be locate or download");}
            else if(key=="zipPreview"){next->zip_preview=string(8);if(next->zip_preview!="off"&&next->zip_preview!="host"&&next->zip_preview!="client")throw Failure(400,"ZIP preview must be off, host or client");}
            else if(key=="previewAllowed")next->preview=boolean();
            else if(key=="thumbnailsEnabled")next->thumbnails=boolean();
            else if(key=="magikaEnabled")next->magika=boolean();
            else if(key=="mcpEnabled")next->mcp=boolean();
            else if(key=="p2pEnabled"){next->p2p=boolean();}
            else if(key=="p2pCacheMiB")next->p2p_cache_mb=static_cast<unsigned>(number(0,64));
            else if(key=="p2pCacheSeconds")next->p2p_cache_seconds=static_cast<unsigned>(number(1,86400));
            else if(key=="ipLimit")next->ip_limit=static_cast<std::size_t>(number(0,1000000));
            else if(key=="ownerLimit")next->owner_limit=static_cast<std::size_t>(number(0,1000000));
            else if(key=="maxUploadMiB")next->max_upload_bytes=number(1,1048576)*(1ull<<20);
            else if(key=="ipUploadMiB")next->ip_upload_bytes=number(0,1073741824)*(1ull<<20);
            else if(key=="ipUploadOverrides"){
                if(!value.is_object()||value.as_object().size()>256)throw Failure(400,"At most 256 per-IP limits");
                std::map<std::string,std::uint64_t> limits;
                for(const auto& entry:value.as_object()){
                    boost::system::error_code address_error;const auto address=boost::asio::ip::make_address(std::string(entry.key()),address_error);if(address_error)throw Failure(400,"Invalid IP allowance address");
                    if(!entry.value().is_uint64()&&!entry.value().is_int64())throw Failure(400,"Expected an integer MiB allowance");
                    if(entry.value().is_int64()&&entry.value().as_int64()<0)throw Failure(400,"Negative upload allowance");
                    const auto amount=entry.value().is_uint64()?entry.value().as_uint64():static_cast<std::uint64_t>(entry.value().as_int64());if(amount>1073741824)throw Failure(400,"Upload allowance too large");limits[address.to_string()]=amount*(1ull<<20);
                }
                next->ip_upload_overrides=std::move(limits);
            }
            else if(key=="title"){next->title=string(128);if(next->title.empty())throw Failure(400,"Title cannot be empty");}
            else if(key=="announcement")next->announcement=string(4096);
            else throw Failure(400,"Unknown setting: "+key);
        }
        if(next->login_password!=settings()->login_password)++next->access_revision;
        config_store_.save(config_,*next);
        ++next->revision;std::shared_ptr<const LiveSettings> snapshot=next;{std::lock_guard<std::mutex> access(access_mutex_);std::atomic_store(&live_,std::move(snapshot));}
        {std::lock_guard<std::mutex> peers(p2p_.mutex);p2p_.prune(next->p2p?next->access_revision:0);}
        // Recognition results must not retain the previous inference policy.
        {std::lock_guard<std::mutex> type_lock(type_mutex_);types_.clear();}
    }
    const auto state=settings();auto result=published(*state);
    result["recycleEnabled"]=state->recycle;result["recycleDays"]=state->recycle_days;result["recycleMiB"]=state->recycle_bytes>>20;result["ipTransfers"]=state->ip_transfers;
    result["uploadKiB"]=state->upload_kbps;result["downloadKiB"]=state->download_kbps;result["ipUploadKiB"]=state->ip_upload_kbps;result["ipDownloadKiB"]=state->ip_download_kbps;
    result["scriptEnabled"]=scripts_.enabled();result["scriptEngine"]="QuickJS-NG 0.18.0";result["wasmEngine"]="WAMR 2.4.5";result["wasmEnabled"]=config_.wasm;result["workspaceAccess"]=config_.workspace_access;result["workspaceShare"]=config_.workspace_share;result["scriptFilesystem"]=config_.script_fs;result["mcpProtocol"]="2026-07-28";
    result["zipPreview"]=state->zip_preview;
    result["writeMode"]=state->write_mode;result["previewAllowed"]=state->preview;result["magikaEnabled"]=state->magika;result["mcpEnabled"]=state->mcp;
    result["ipLimit"]=state->ip_limit;result["ownerLimit"]=state->owner_limit;result["maxUploadMiB"]=state->max_upload_bytes>>20;
    result["ipUploadMiB"]=state->ip_upload_bytes>>20;
    json::object limits;for(const auto& item:state->ip_upload_overrides)limits[item.first]=item.second>>20;result["ipUploadOverrides"]=std::move(limits);
    result["loginRequired"]=!state->login_password.empty();result["hotlinkSeconds"]=state->hotlink_seconds;result["strictHost"]=config_.strict_host;result["compression"]=config_.compression;
    result["p2pPeers"]=config_.p2p_peers;result["p2pUploadKiB"]=config_.p2p_upload_kbps;
    result["mcpPath"]="/mcp";result["mcpToken"]=mcp_token_;result["persistence"]=config_store_.enabled()?"config":"process";
    auto response=encode(std::move(result));if(request.method=="POST"&&(!state->login_password.empty()||state->hotlink_seconds))new_access(request,response,!state->login_password.empty());return response;
}
Response Service::admin_attention(const Request& request){
    if(!request.local_admin)throw Failure(403,"Only the host can mark attention files");
    browser_guard(request,true);
    if(request.method!="POST")throw Failure(405,"Use POST to mark or unmark one file");
    if(request.body.size()>65536)throw Failure(413,"Attention request is too large");
    boost::system::error_code ec;json::parse_options options;options.max_depth=4;
    const auto document=json::parse(request.body,ec,{},options);
    if(ec||!document.is_object())throw Failure(400,"Expected an attention object");
    const auto& object=document.as_object();const auto* path=object.if_contains("path");const auto* marked=object.if_contains("marked");
    if(object.size()!=2||!path||!path->is_string()||path->as_string().empty()||path->as_string().size()>4096||!marked||!marked->is_bool())
        throw Failure(400,"Expected path and marked fields");
    const auto logical=normalize_path(std::string(path->as_string()));
    std::lock_guard<std::mutex> lock(write_mutex_);
    const auto state=settings();auto next=std::make_shared<LiveSettings>(*state);
    auto& paths=next->attention_paths;const auto found=std::find(paths.begin(),paths.end(),logical);
    if(marked->as_bool()){
        open_file(logical); // Safe, regular files only; no links or directories.
        if(found==paths.end()){
            if(paths.size()>=128)throw Failure(409,"At most 128 attention files may be marked");
            paths.push_back(logical);
        }else return encode(published(*state));
    }else{
        if(found==paths.end())return encode(published(*state));
        paths.erase(found); // Also permits clearing a file removed outside this process.
    }
    config_store_.save(config_,*next);
    ++next->revision;std::shared_ptr<const LiveSettings> snapshot=next;std::atomic_store(&live_,std::move(snapshot));
    return encode(published(*next));
}
// Called under write_mutex_ after a successful file/directory mutation.
void Service::move_attention(const std::string& from,const std::string& to){
    const auto state=settings();auto next=std::make_shared<LiveSettings>(*state);bool changed=false;
    auto& paths=next->attention_paths;
    for(auto entry=paths.begin();entry!=paths.end();){
        if(*entry==from||(entry->size()>from.size()&&entry->compare(0,from.size(),from)==0&&(*entry)[from.size()]=='/')){
            changed=true;
            if(to.empty()){entry=paths.erase(entry);continue;}
            *entry=to+entry->substr(from.size());
        }
        ++entry;
    }
    if(changed){config_store_.save(config_,*next);++next->revision;std::shared_ptr<const LiveSettings> snapshot=next;std::atomic_store(&live_,std::move(snapshot));}
}
}
