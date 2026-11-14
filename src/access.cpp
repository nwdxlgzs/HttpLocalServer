// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <boost/asio/ip/address.hpp>
#include <algorithm>

namespace hls {
namespace {
using Clock=std::chrono::steady_clock;
std::string session_id(const Request& r){
    const auto cookie=r.header("cookie");std::size_t at=0;
    while(at<cookie.size()){
        while(at<cookie.size()&&(cookie[at]==' '||cookie[at]==';'))++at;
        const auto end=cookie.find(';',at);const auto part=cookie.substr(at,end==std::string::npos?end:end-at);
        if(part.rfind("HLS_SID=",0)==0){auto id=part.substr(8);return id.size()==48?id:"";}
        if(end==std::string::npos)break;
        at=end+1;
    }return {};
}
bool equal_secret(const std::string& a,const std::string& b){
    // Fixed work for all permitted passphrase lengths, including unequal ones.
    unsigned difference=static_cast<unsigned>(a.size()^b.size());
    for(std::size_t i=0;i<1024;++i)difference|=(i<a.size()?static_cast<unsigned char>(a[i]):0)^(i<b.size()?static_cast<unsigned char>(b[i]):0);
    return difference==0;
}
Response encode(boost::json::object value,unsigned status=200){Response r;r.status=status;r.body=boost::json::serialize(value);r.headers={{"Cache-Control","no-store"}};return r;}
unsigned seconds_left(Clock::time_point until){const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(until-Clock::now()).count();return ms>0?static_cast<unsigned>((ms+999)/1000):0;}
}
std::string Service::new_access(const Request& request,Response& response,bool authenticated,std::uint64_t expected_revision){
    std::lock_guard<std::mutex> lock(access_mutex_);
    if(expected_revision&&settings()->access_revision!=expected_revision)throw Failure(409,"Login settings changed; please retry");
    const auto now=Clock::now();
    for(auto it=access_sessions_.begin();it!=access_sessions_.end();)if(now-it->second.created>=std::chrono::hours(24))it=access_sessions_.erase(it);else ++it;
    const auto current=access_sessions_.find(session_id(request));
    if(current!=access_sessions_.end()&&current->second.ip==request.peer_ip&&current->second.revision==settings()->access_revision&&current->second.authenticated==authenticated){current->second.last=now;return current->first;}
    // Rotate rather than fixing an attacker-chosen cookie at login.
    access_sessions_.erase(session_id(request));
    if(access_sessions_.size()>=8192){const auto victim=std::min_element(access_sessions_.begin(),access_sessions_.end(),[](const auto& a,const auto& b){return a.second.last<b.second.last;});access_sessions_.erase(victim);}
    const auto id=random_token();access_sessions_[id]={request.peer_ip,settings()->access_revision,authenticated,now,now};
    response.headers.emplace_back("Set-Cookie","HLS_SID="+id+"; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400"+(request.secure?"; Secure":""));
    response.headers.emplace_back("Cache-Control","no-store");return id;
}
bool Service::access_guard(const Request& request,bool downloading,bool activity){
    const auto state=settings();if(state->login_password.empty()&&!state->hotlink_seconds&&request.preview_key.empty())return true;
    const auto now=Clock::now();std::lock_guard<std::mutex> lock(access_mutex_);
    if(!request.preview_key.empty()){
        const auto grant=preview_grants_.find(request.preview_key);
        if(grant==preview_grants_.end()||grant->second.ip!=request.peer_ip||grant->second.revision!=state->access_revision||now-grant->second.created>=std::chrono::hours(24))throw Failure(403,"Preview grant expired; reopen the preview");
        if(!state->login_password.empty()){
            const auto owner=access_sessions_.find(grant->second.session);if(owner==access_sessions_.end()||owner->second.revision!=state->access_revision||!owner->second.authenticated)throw Failure(401,"Login required");
        }
        auto last=grant->second.last;const auto owner=access_sessions_.find(grant->second.session);if(owner!=access_sessions_.end())last=std::max(last,owner->second.last);
        if(state->hotlink_seconds&&now-last>=std::chrono::seconds(state->hotlink_seconds))throw Failure(403,"Preview session expired; reopen the preview");
        if(downloading&&activity){grant->second.last=now;const auto owner=access_sessions_.find(grant->second.session);if(owner!=access_sessions_.end())owner->second.last=now;}
        return true;
    }
    const auto found=access_sessions_.find(session_id(request));
    const bool valid=found!=access_sessions_.end()&&found->second.ip==request.peer_ip&&found->second.revision==state->access_revision&&now-found->second.created<std::chrono::hours(24);
    if(!state->login_password.empty()&&(!valid||!found->second.authenticated))return false;
    if(downloading&&state->hotlink_seconds){
        const auto site=lower_ascii(request.header("sec-fetch-site"));
        const auto referer=lower_ascii(request.header("referer")),prefix=std::string(request.secure?"https://":"http://")+lower_ascii(request.header("host"))+"/";
        if(site=="cross-site"||(!referer.empty()&&referer.rfind(prefix,0)!=0))throw Failure(403,"External download links are disabled");
        if(!valid||now-found->second.last>=std::chrono::seconds(state->hotlink_seconds))throw Failure(403,"Download session expired; return to the file page and resume access");
        if(activity)found->second.last=now;
    }
    return true;
}
void Service::transfer_activity(const Request& request){
    const auto state=settings();if(state->login_password.empty()&&!state->hotlink_seconds)return;
    std::string key=request.preview_key;const std::string prefix="/preview-assets/";if(key.empty()&&request.target.rfind(prefix,0)==0)key=request.target.substr(prefix.size(),48);
    const std::string zip_prefix="/zip-assets/";if(key.empty()&&request.target.rfind(zip_prefix,0)==0&&request.target.size()>=zip_prefix.size()+48)key=request.target.substr(zip_prefix.size(),48);
    std::lock_guard<std::mutex> lock(access_mutex_);const auto found=access_sessions_.find(session_id(request));
    if(found!=access_sessions_.end()&&found->second.ip==request.peer_ip)found->second.last=Clock::now();
    const auto grant=preview_grants_.find(key);if(grant!=preview_grants_.end()&&grant->second.ip==request.peer_ip){grant->second.last=Clock::now();const auto owner=access_sessions_.find(grant->second.session);if(owner!=access_sessions_.end())owner->second.last=Clock::now();}
}
void Service::maintain_access(){
    std::lock_guard<std::mutex> lock(access_mutex_);const auto now=Clock::now();
    for(auto it=access_sessions_.begin();it!=access_sessions_.end();)if(now-it->second.created>=std::chrono::hours(24))it=access_sessions_.erase(it);else ++it;
    const auto state=settings();
    for(auto it=preview_grants_.begin();it!=preview_grants_.end();)if(it->second.revision!=state->access_revision||now-it->second.created>=std::chrono::hours(24))it=preview_grants_.erase(it);else ++it;
    for(auto it=login_attempts_.begin();it!=login_attempts_.end();)if(now-it->second.used>std::chrono::hours(48)&&it->second.until<=now){state_.erase("login",it->first);it=login_attempts_.erase(it);}else ++it;
}
Response Service::access_api(const Request& request,const std::string& route){
    namespace json=boost::json;
    if(route=="/api/admin/access"){
        if(!request.local_admin)throw Failure(403,"Only the host can unblock an IP");
        if(!access_guard(request))throw Failure(401,"Login required");
        browser_guard(request,request.method!="GET");
        if(request.method=="POST"){
            boost::system::error_code ec;auto data=json::parse(request.body,ec);if(ec||!data.is_object()||!data.as_object().if_contains("ip")||!data.at("ip").is_string())throw Failure(400,"Expected an IP address");
            const auto address=boost::asio::ip::make_address(std::string(data.at("ip").as_string()),ec);if(ec)throw Failure(400,"Invalid IP address");
            std::lock_guard<std::mutex> lock(access_mutex_);state_.erase("login",address.to_string());login_attempts_.erase(address.to_string());
        }else if(request.method!="GET")throw Failure(405,"Use GET or POST");
        json::array ips;std::lock_guard<std::mutex> lock(access_mutex_);
        for(const auto& entry:login_attempts_){const auto remaining=seconds_left(entry.second.until);if(remaining)ips.emplace_back(json::object{{"ip",entry.first},{"retryAfter",remaining},{"round",entry.second.round}});}
        return encode({{"blocked",std::move(ips)}});
    }
    browser_guard(request,false);
    if(route=="/api/access/activate"){
        browser_guard(request,true);if(request.method!="POST")throw Failure(405,"Use POST");
        if(!access_guard(request))throw Failure(401,"Login required");
        Response r=encode({{"ok",true}});if(!settings()->login_password.empty()||settings()->hotlink_seconds)new_access(request,r,!settings()->login_password.empty());return r;
    }
    if(route!="/api/access/login"||request.method!="POST")throw Failure(405,"Use POST to log in");
    if(settings()->login_password.empty())throw Failure(400,"Login is not enabled");
    if(request.header("content-type").rfind("application/json",0)!=0)throw Failure(415,"Login requires JSON");
    boost::system::error_code ec;json::parse_options options;options.max_depth=2;const auto data=json::parse(request.body,ec,{},options);
    if(ec||!data.is_object()||!data.as_object().if_contains("password")||!data.at("password").is_string()||data.at("password").as_string().size()>1024)throw Failure(400,"Invalid passphrase");
    const auto supplied=std::string(data.at("password").as_string());const auto policy=settings();unsigned wait=0,remaining=5;bool accepted=false;
    {
        std::lock_guard<std::mutex> lock(access_mutex_);const auto now=Clock::now();
        if(login_attempts_.size()>=65536&&!login_attempts_.count(request.peer_ip))throw Failure(503,"Login accounting capacity reached");
        auto& attempt=login_attempts_[request.peer_ip];attempt.used=now;wait=seconds_left(attempt.until);
        if(!wait){
            accepted=equal_secret(supplied,policy->login_password);
            if(accepted){state_.erase("login",request.peer_ip);login_attempts_.erase(request.peer_ip);}
            else{wait=attempt.failed(now);remaining=5-attempt.failures;persist_login(request.peer_ip,attempt);}
        }
    }
    if(accepted){auto r=encode({{"ok",true}});new_access(request,r,true,policy->access_revision);return r;}
    auto r=encode({{"error",wait?"IP temporarily blocked":"Incorrect passphrase"},{"remaining",wait?0:remaining},{"retryAfter",wait}},wait?429:401);
    if(wait)r.headers.emplace_back("Retry-After",std::to_string(wait));
    return r;
}
Response Service::preview_redirect(const Request& request,const std::string& path){
    if(!settings()->preview)throw Failure(403,"Preview is disabled");
    if(!access_guard(request,true))throw Failure(401,"Login required");
    auto key=random_token();const auto now_id=session_id(request);const auto now=Clock::now();
    {std::lock_guard<std::mutex> lock(access_mutex_);bool found=false;
        for(auto& grant:preview_grants_)if(grant.second.ip==request.peer_ip&&grant.second.session==now_id&&grant.second.revision==settings()->access_revision){key=grant.first;grant.second.last=now;found=true;break;}
        if(!found){if(preview_grants_.size()>=8192){const auto victim=std::min_element(preview_grants_.begin(),preview_grants_.end(),[](const auto& a,const auto& b){return a.second.last<b.second.last;});preview_grants_.erase(victim);}preview_grants_[key]={request.peer_ip,now_id,settings()->access_revision,now,now};}
    }
    std::string encoded;for(unsigned char c:path)encoded+=c=='/'?"/":url_encode(std::string(1,static_cast<char>(c)));
    const auto question=request.target.find('?');Response r;r.status=302;r.headers={{"Location","/preview-assets/"+key+encoded+(question==std::string::npos?"":request.target.substr(question))},{"Cache-Control","no-store"}};return r;
}
Response Service::login_page(const Request&){
    Response r;r.content_type="text/html; charset=utf-8";
    r.headers={{"Cache-Control","no-store"},{"X-Frame-Options","DENY"},{"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'"}};
    r.body=R"HLS(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>登录 · HttpLocalServer</title><style>html{color-scheme:light dark}body{margin:0;background:#edf2f8;color:#273c57;font:15px 'Segoe UI',sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh}form{width:300px;margin:24px;background:#fff;padding:36px;border:1px solid #cdd9e8;border-radius:18px;box-shadow:0 20px 70px rgba(33,59,87,.094)}small{color:#7690ad}h1{font-size:24px;margin:10px 0 28px}input,button{box-sizing:border-box;width:100%;font:inherit;border-radius:8px;padding:12px;border:1px solid #b8c9df}button{margin-top:16px;background:#2868b3;color:white;border:0;cursor:pointer}button:disabled{opacity:.45}#message{line-height:1.6;font-size:13px;min-height:42px;color:#ad4c3f}@media(prefers-color-scheme:dark){body{background:#121c2b;color:#e3edfc}form{background:#1d2c41;border-color:#40526d}input{background:#172438;color:inherit}}</style><form id="login"><small>HttpLocalServer</small><h1>登录共享空间</h1><input id="password" type="password" maxlength="1024" autocomplete="current-password" placeholder="登录口令" aria-label="登录口令" autofocus required><button id="submit">登录</button><p id="message" role="alert"></p></form><script>(function(){var form=document.getElementById('login'),button=document.getElementById('submit'),message=document.getElementById('message'),until=0,timer=0;function tick(){var left=Math.max(0,Math.ceil((until-new Date().getTime())/1000));button.disabled=left>0;message.textContent=left?'请在 '+left+' 秒后重试':'';if(!left)clearInterval(timer);}form.onsubmit=function(event){event.preventDefault();if(button.disabled)return;button.disabled=true;var xhr=new XMLHttpRequest();xhr.open('POST','/api/access/login',true);xhr.setRequestHeader('Content-Type','application/json');xhr.onreadystatechange=function(){if(xhr.readyState!==4)return;button.disabled=false;var data;try{data=JSON.parse(xhr.responseText);}catch(ignore){message.textContent='连接失败，请重试';return;}if(xhr.status===200){window.location.reload();return;}if(data.retryAfter){until=new Date().getTime()+data.retryAfter*1000;clearInterval(timer);timer=setInterval(tick,1000);tick();}else{message.textContent='口令不正确，剩余 '+(data.remaining||0)+' 次机会';document.getElementById('password').select();}};xhr.send(JSON.stringify({password:document.getElementById('password').value}));};}());</script></html>)HLS";
    return r;
}
}
