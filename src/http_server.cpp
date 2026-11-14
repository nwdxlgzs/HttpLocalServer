#include "version.hpp"
#include "http_server.hpp"
#include "service.hpp"
#include "util.hpp"
#include "network_features.hpp"
#include "tls.hpp"
#include <sstream>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <algorithm>
#include <csignal>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace hls {
namespace net=boost::asio;
namespace beast=boost::beast;
namespace http=beast::http;
using tcp=net::ip::tcp;
namespace {
struct ActiveRequest {
    ActiveRequest(RuntimeStats& value,bool is_control,std::atomic<std::size_t>* selected=nullptr):stats(value),counter(selected?*selected:is_control?value.active_control:value.active_transfers),control(is_control) { stats.active.fetch_add(1); }
    ~ActiveRequest() { stats.active.fetch_sub(1); counter.fetch_sub(1); }
    RuntimeStats& stats; std::atomic<std::size_t>& counter;
    bool control;
};
struct IpTracker {
    std::atomic<unsigned> handshakes{0};
    std::mutex mutex; std::map<std::string,std::size_t> ips; std::size_t limit;
    explicit IpTracker(std::size_t value):limit(value){}
    bool acquire(const std::string& ip,std::size_t current_limit){std::lock_guard<std::mutex> lock(mutex);auto it=ips.find(ip);if(it==ips.end()){if(current_limit && ips.size()>=current_limit)return false;ips[ip]=1;}else ++it->second;return true;}
    void release(const std::string& ip){std::lock_guard<std::mutex> lock(mutex);auto it=ips.find(ip);if(it!=ips.end() && !--it->second)ips.erase(it);}
};
Response exception_response(){
    try{throw;}catch(const Failure& error){return Service::error(error.status,error.what());}
    catch(const std::invalid_argument& error){return Service::error(400,error.what());}
    catch(const std::exception&){return Service::error(500,"Storage operation failed");}
}
std::string authority(const tcp::endpoint& endpoint,bool secure){
    const auto address=endpoint.address().to_string();
    return (endpoint.address().is_v6()?"["+address+"]":address)+(endpoint.port()==(secure?443:80)?"":":"+std::to_string(endpoint.port()));
}
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket,Service& service,WorkerPool& workers,WorkerPool& disk,RuntimeStats& runtime,const Config& config,std::shared_ptr<IpTracker> tracker,const std::vector<std::string>& local,std::shared_ptr<TlsContext> tls,bool control=false)
        :stream_(std::move(socket),std::move(tls)),throttle_(stream_.get_executor()),buffer_(32768),service_(service),workers_(workers),disk_(disk),runtime_(runtime),config_(config),tracker_(std::move(tracker)) {
        const auto peer=stream_.socket().remote_endpoint().address();peer_=peer.to_string();
        local_admin_=config.strict_host?(peer_=="127.0.0.1"||peer_=="::1"):(peer.is_loopback() || std::find(local.begin(),local.end(),peer_)!=local.end());
        local_device_=local_admin_;control_channel_=control;application_listener_=!config.workspace_app.empty()&&!control;
        if(application_listener_)local_admin_=false;
        authority_=authority(stream_.socket().local_endpoint(),stream_.secure());
    }
    ~Session(){release_handshake();runtime_.connections.fetch_sub(1);tracker_->release(peer_);}
    void start(){
        beast::error_code ignored;stream_.socket().set_option(tcp::no_delay(true),ignored);
        if(!stream_.secure()){read();return;}
        if(tracker_->handshakes.fetch_add(1)>=config_.tls_handshakes){tracker_->handshakes.fetch_sub(1);runtime_.rejected.fetch_add(1);close();return;}
        handshaking_=true;stream_.expires_at(std::chrono::steady_clock::now()+std::chrono::seconds(config_.tls_handshake_timeout));
        stream_.async_handshake([self=shared_from_this()](beast::error_code ec,std::size_t){self->release_handshake();if(ec)self->close();else self->read();});
    }
private:
    void release_handshake(){if(handshaking_){handshaking_=false;tracker_->handshakes.fetch_sub(1);}}
    void read(){
        if(closed_)return;
        parser_.emplace();parser_->body_limit(std::max<std::uint64_t>(4u*1024u*1024u,service_.settings()->max_upload_bytes));parser_->header_limit(16384);
        stream_.expires_after(std::chrono::seconds(config_.timeout));
        http::async_read_header(stream_,buffer_,*parser_,[self=shared_from_this()](beast::error_code error,std::size_t){self->on_read(error);});
    }
    void on_read(beast::error_code error){
        if(error==http::error::end_of_stream || error==net::error::eof || error==beast::error::timeout || error==net::error::operation_aborted){close();return;}
        if(error){keep_alive_=false;respond(Service::error(error==http::error::body_limit?413:400,"Malformed or unsupported HTTP request"));return;}
        const auto& message=parser_->get();version_=message.version();keep_alive_=message.keep_alive();head_=message.method()==http::verb::head;
        runtime_.requests.fetch_add(1);
        Request request;request.method=std::string(message.method_string());request.target=std::string(message.target());
        request.peer_ip=peer_;request.local_admin=local_admin_;request.local_device=local_device_;request.application_listener=application_listener_;request.control_channel=control_channel_;request.authority=authority_;request.secure=stream_.secure();
        for(const auto& field:message){
            const auto name=lower_ascii(std::string(field.name_string()));
            if(request.headers.count(name) && (name=="range" || name=="host" || name=="origin" || name=="x-hls-token" || name=="x-hls-app" || name=="mcp-method" || name=="mcp-name" || name=="expect" || name=="authorization" || name=="mcp-protocol-version" || name=="cookie")){keep_alive_=false;respond(Service::error(400,"Duplicate request header"));return;}
            if(name=="accept-encoding"&&request.headers.count(name))request.headers[name]+=","+std::string(field.value());
            else request.headers[name]=std::string(field.value());
        }
        if(request.target.size()>8192 || request.target.empty() || request.target[0]!='/'){keep_alive_=false;respond(Service::error(400,"Invalid request target"));return;}
        if(version_==11 && request.header("host").empty()){keep_alive_=false;respond(Service::error(400,"Host header is required"));return;}
        request_=request;
        const bool uploading=request.method=="PUT" && request.target.substr(0,request.target.find('?'))=="/api/upload";
        const auto route=request.target.substr(0,request.target.find('?'));
        const bool control_body=(request.method=="POST"||request.method=="PUT"||(!config_.workspace_app.empty()&&(request.method=="PATCH"||request.method=="DELETE"))) && (((!config_.script_path.empty()||!config_.workspace_app.empty())&&route.rfind("/api/",0)!=0)||(!config_.workspace_app.empty()&&route.rfind("/api/app/",0)==0)||route.rfind("/api/zip/",0)==0||route.rfind("/api/resume/",0)==0||route.rfind("/api/transfer-cache/",0)==0||route.rfind("/api/recycle",0)==0||route=="/api/admin/state"||route=="/api/batch"||route=="/mcp" || route.rfind("/api/access/",0)==0 || route.rfind("/api/p2p/",0)==0 || route=="/api/admin/access" || route=="/api/admin/settings" || route=="/api/admin/attention" || route=="/api/admin/aliases" || route=="/api/alias" || route=="/api/browse" || route.rfind("/api/mount/",0)==0);
        if(!uploading && !control_body && !parser_->is_done()){keep_alive_=false;respond(Service::error(400,"Request bodies are only accepted for uploads"));return;}
        const bool payload_transfer=uploading||route=="/api/transfer-cache/chunk"||route=="/api/transfer-cache/copy-chunk"||route=="/api/transfer-cache/publish-mount"||route=="/api/transfer-cache/read"||route=="/api/resume/copy-chunk"||route=="/api/resume/chunk"||route=="/api/resume/commit"||route=="/api/mount/upload-chunk";
        const bool control=!payload_transfer && (request.target.rfind("/api/",0)==0 || request.target=="/mcp");
        // Deferred consumers must never consume the slots their provider needs
        // to complete them. Mirror uploads similarly bypass waiting downloads.
        const bool provider_control=route=="/api/mount/poll"||route=="/api/mount/reply"||route=="/api/mount/payload";
        bool provider_upload=false;
        if(uploading){try{const auto query=query_params(request.target);const auto mirror=query.find("mirror");provider_upload=mirror!=query.end()&&!mirror->second.empty();}catch(const std::exception&){keep_alive_=false;respond(Service::error(400,"Invalid upload query"));return;}}
        auto& counter=provider_control?runtime_.active_provider_control:provider_upload?runtime_.active_provider_uploads:control?runtime_.active_control:runtime_.active_transfers;
        const auto limit=provider_control?std::size_t(32):provider_upload?std::size_t(8):control?std::size_t(8):config_.max_transfers;
        if(counter.fetch_add(1)>=limit){counter.fetch_sub(1);runtime_.rejected.fetch_add(1);keep_alive_=false;respond(Service::error(503,"Active request limit reached; retry later"));return;}
        lease_=std::make_shared<ActiveRequest>(runtime_,control,&counter);
        if(payload_transfer){
            if(!service_.traffic().acquire(peer_,service_.settings()->ip_transfers,true)){keep_alive_=false;respond(Service::error(429,"Per-IP transfer concurrency reached"));return;}
            traffic_lease_=std::shared_ptr<void>(reinterpret_cast<void*>(1),[controller=service_.traffic_handle(),ip=peer_](void*){controller->release(ip,true);});
        }
        if(uploading){
            request_=std::move(request);
            if(!workers_.submit([self=shared_from_this()]{
                std::shared_ptr<Upload> upload;Response failure;
                try{upload=self->service_.begin_upload(self->request_);}catch(...){failure=exception_response();}
                net::post(self->stream_.get_executor(),[self,upload=std::move(upload),failure=std::move(failure)]()mutable{
                    if(self->closed_)return;
                    if(!upload){self->keep_alive_=false;self->respond(std::move(failure));return;}
                    self->upload_=std::move(upload);
                    try{self->incoming_=self->service_.cache().transfer_buffer();}catch(...){self->keep_alive_=false;self->respond(exception_response());return;}
                    const auto expect=lower_ascii(self->request_.header("expect"));
                    if(!expect.empty() && expect!="100-continue"){self->keep_alive_=false;self->respond(Service::error(417,"Unsupported expectation"));return;}
                    if(expect=="100-continue")net::async_write(self->stream_,net::buffer("HTTP/1.1 100 Continue\r\n\r\n",25),[self](beast::error_code ec,std::size_t){if(ec)self->close();else self->read_upload();});
                    else self->read_upload();
                });
            }))queue_full();
            return;
        }
        if(control_body){
            if(!local_admin_&&(route=="/mcp"||route.rfind("/api/admin/",0)==0||route=="/api/mount/native")){keep_alive_=false;respond(Service::error(403,"Local control endpoint"));return;}
            control_limit_=route=="/api/transfer-cache/checkpoint"?4u*1024u*1024u:route=="/api/transfer-cache/chunk"||route=="/api/resume/chunk"?1048576:route=="/api/mount/reply"||route=="/api/mount/upload-chunk"?262144:65536;
            if(parser_->content_length() && *parser_->content_length()>control_limit_){keep_alive_=false;respond(Service::error(413,"Control body exceeds limit"));return;}
            request_=std::move(request);upload_buffer_.resize(8192);read_control();return;
        }
        if(!workers_.submit([self=shared_from_this(),request=std::move(request)]{
            auto response=self->service_.handle(request);
            net::post(self->stream_.get_executor(),[self,response=std::move(response)]()mutable{if(!self->closed_)self->respond(std::move(response));});
        }))queue_full();
    }
    void read_control(){
        if(closed_)return;
        if(parser_->is_done()){
            auto& pool=(request_.target.rfind("/api/resume/commit",0)==0||request_.target.rfind("/api/zip/",0)==0)?disk_:workers_;
            if(!pool.submit([self=shared_from_this()]{auto response=self->service_.handle(self->request_);net::post(self->stream_.get_executor(),[self,response=std::move(response)]()mutable{if(!self->closed_)self->respond(std::move(response));});}))queue_full();
            return;
        }
        auto& body=parser_->get().body();body.data=upload_buffer_.data();body.size=upload_buffer_.size();
        stream_.expires_after(std::chrono::seconds(config_.timeout));
        http::async_read_some(stream_,buffer_,*parser_,[self=shared_from_this()](beast::error_code error,std::size_t){
            if(error==http::error::need_buffer)error={};
            if(error){self->keep_alive_=false;self->respond(Service::error(400,"Invalid control body"));return;}
            const auto size=self->upload_buffer_.size()-self->parser_->get().body().size;
            if(self->request_.body.size()+size>self->control_limit_){self->keep_alive_=false;self->respond(Service::error(413,"Control body exceeds limit"));return;}
            self->runtime_.received_bytes.fetch_add(size);self->request_.body.append(reinterpret_cast<const char*>(self->upload_buffer_.data()),size);
            if(self->request_.target.rfind("/api/resume/chunk",0)==0||self->request_.target.rfind("/api/transfer-cache/chunk",0)==0||self->request_.target.rfind("/api/mount/upload-chunk",0)==0)self->pace_upload(size,[self]{self->read_control();});else self->read_control();
        });
    }
    void queue_full(){runtime_.rejected.fetch_add(1);keep_alive_=false;respond(Service::error(503,"Storage queue is full; retry later"));}
    void read_upload(){
        if(closed_)return;
        if(parser_->is_done()){commit_upload();return;}
        auto& body=parser_->get().body();body.data=incoming_->data;const auto policy=service_.settings();upload_capacity_=service_.traffic().preferred_block(peer_,true,*policy,incoming_->size);body.size=upload_capacity_;
        stream_.expires_after(std::chrono::seconds(config_.timeout));
        // Fill the bounded body buffer before disk/quota work. Reading one
        // parser fragment at a time can fsync SQLite for every 512 network bytes.
        http::async_read(stream_,buffer_,*parser_,[self=shared_from_this()](beast::error_code error,std::size_t){
            if(error==http::error::need_buffer)error={};
            const auto bytes=self->upload_capacity_-self->parser_->get().body().size;
            if(error)self->keep_alive_=false;
            // Account a short final buffer even when its peer closes before the
            // declared length; it must not become a quota bypass or publication.
            if(error&&!bytes){self->upload_.reset();if(error==http::error::body_limit)self->respond(Service::error(413,"Upload exceeds configured limit"));else self->close();return;}
            if(!bytes){self->read_upload();return;}
            self->runtime_.received_bytes.fetch_add(bytes);
            self->pace_upload(bytes,[self,bytes,error]{if(!self->disk_.submit([self,bytes,error,upload=self->upload_]{
                Response failure;bool ok=true;try{upload->append(self->incoming_->data,bytes);}catch(...){ok=false;failure=exception_response();}
                net::post(self->stream_.get_executor(),[self,ok,error,failure=std::move(failure)]()mutable{
                    if(self->closed_)return;
                    if(!ok){self->keep_alive_=false;self->upload_.reset();self->respond(std::move(failure));}
                    else if(error){self->upload_.reset();if(error==http::error::body_limit)self->respond(Service::error(413,"Upload exceeds configured limit"));else self->close();}
                    else self->read_upload();
                });
            }))self->queue_full();});
        });
    }
    void commit_upload(){
        if(!disk_.submit([self=shared_from_this(),upload=upload_]{
            Response response;try{response=self->service_.finish_upload(self->request_,upload);self->service_.encode_response(self->request_,response);}catch(...){response=exception_response();}
            net::post(self->stream_.get_executor(),[self,response=std::move(response)]()mutable{self->upload_.reset();if(!self->closed_)self->respond(std::move(response));});
        }))queue_full();
    }
    void respond(Response response) {
        if(closed_) return;
        if(response.deferred){
            // Keep bounded control responses in the control budget; provider
            // heartbeats/replies have independent slots to finish these jobs.
            pending_cancel_=std::move(response.cancel);
            response.deferred([self=shared_from_this()](Response ready){net::post(self->stream_.get_executor(),[self,ready=std::move(ready)]()mutable{self->pending_cancel_={};if(!self->closed_)self->respond(std::move(ready));});});return;
        }
        const bool provider_body=request_.target.rfind("/api/mount/payload",0)==0;
        if(response.file&&lease_&&lease_->control&&!provider_body){lease_.reset();if(runtime_.active_transfers.fetch_add(1)>=config_.max_transfers){runtime_.active_transfers.fetch_sub(1);respond(Service::error(503,"Mount transfer budget reached"));return;}lease_=std::make_shared<ActiveRequest>(runtime_,false);}
        const bool payload_body=!response.body.empty()&&response.status<400&&response.content_type.rfind("application/json",0)!=0;
        if((response.file||payload_body)&&!traffic_lease_){if(!service_.traffic().acquire(peer_,provider_body?0:service_.settings()->ip_transfers)){respond(Service::error(429,"Per-IP transfer concurrency reached"));return;}traffic_lease_=std::shared_ptr<void>(reinterpret_cast<void*>(1),[controller=service_.traffic_handle(),ip=peer_](void*){controller->release(ip);});}
        response_=std::move(response);
        if(response_.gzip_stream){gzip_=std::make_unique<GzipEncoder>(response_.deflate_stream);if(version_<11)keep_alive_=false;}
        serializer_.reset(); header_.emplace(static_cast<http::status>(response_.status),version_);
        header_->set(http::field::server,"HttpLocalServer/" HLS_VERSION);
        header_->set(http::field::date,http_date(std::time(nullptr)));
        header_->set(http::field::content_type,response_.content_type);
        header_->set("X-Content-Type-Options","nosniff"); header_->set("Referrer-Policy","same-origin");
        for(const auto& field:response_.headers) header_->set(field.first,field.second);
        header_->keep_alive(keep_alive_);
        if(response_.gzip_stream){if(version_>=11)header_->chunked(true);}
        else if(response_.status!=304 && response_.status!=204) header_->content_length(response_.file?response_.length:response_.body.size());
        serializer_=std::make_unique<http::response_serializer<http::empty_body>>(*header_);
        stream_.expires_after(std::chrono::seconds(config_.timeout));
        http::async_write_header(stream_,*serializer_,[self=shared_from_this()](beast::error_code error,std::size_t) {
            if(error) { self->close(); return; }
            if(self->head_ || self->response_.status==304 || self->response_.status==204) { self->finish(); return; }
            if(self->response_.file) { self->position_=self->response_.offset; self->remaining_=self->response_.length; self->next_block(); }
            else if(self->response_.body.empty()) self->finish();
            else {self->body_position_=0;self->send_body();}
        });
    }
    void send_body(){
        if(closed_)return;if(body_position_>=response_.body.size()){finish();return;}
        const auto bytes=std::min<std::size_t>(16384,response_.body.size()-body_position_);
        auto send=[self=shared_from_this(),bytes]{self->stream_.expires_after(std::chrono::seconds(self->config_.timeout));net::async_write(self->stream_,net::buffer(self->response_.body.data()+self->body_position_,bytes),[self,bytes](beast::error_code ec,std::size_t sent){self->runtime_.sent_bytes.fetch_add(sent);if(ec)self->close();else{self->body_position_+=bytes;self->send_body();}});};
        if((request_.target.rfind("/api/",0)==0&&response_.content_type.rfind("application/json",0)==0)||response_.status>=400)send();else paced(bytes,false,std::move(send));
    }
    void pace_upload(std::size_t bytes,std::function<void()> done){
        upload_pacing_+=bytes;const auto policy=service_.settings();const auto threshold=service_.traffic().preferred_block(peer_,true,*policy,16384);
        if(upload_pacing_<threshold&&!parser_->is_done()){done();return;}
        const auto accumulated=upload_pacing_;upload_pacing_=0;paced(accumulated,true,std::move(done));
    }
    void paced(std::size_t bytes,bool upload,std::function<void()> done){
        if(closed_)return;const auto delay=service_.traffic().delay(peer_,bytes,upload,*service_.settings());
        if(delay<=std::chrono::steady_clock::duration::zero()){done();return;}
        throttle_.expires_after(std::min(delay,std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250))));throttle_.async_wait([self=shared_from_this(),bytes,upload,done=std::move(done)](beast::error_code ec){if(!ec&&!self->closed_)self->paced(bytes,upload,std::move(done));});
    }
    void next_block() {
        if(closed_) return;
        if(!remaining_) {
            if(gzip_&&version_>=11){gzip_.reset();net::async_write(stream_,net::buffer("0\r\n\r\n",5),[self=shared_from_this()](beast::error_code ec,std::size_t){if(ec)self->close();else self->finish();});}
            else finish();
            return;
        }
        const auto block_size=service_.cache().block_size();
        const auto offset=(position_/block_size)*block_size;
        service_.cache().get(response_.file,offset,[self=shared_from_this(),offset](std::exception_ptr error,std::shared_ptr<Block> block) {
            net::post(self->stream_.get_executor(),[self,offset,error,block=std::move(block)] {
                if(self->closed_) return;
                if(error) { self->runtime_.transfer_errors.fetch_add(1); self->close(); return; }
                const auto within=static_cast<std::size_t>(self->position_-offset);
                if(within>=block->size) { self->runtime_.transfer_errors.fetch_add(1); self->close(); return; }
                const auto policy=self->service_.settings();const auto chunk=self->service_.traffic().preferred_block(self->peer_,false,*policy,block->size);
                const auto bytes=static_cast<std::size_t>(std::min<std::uint64_t>(self->remaining_,std::min<std::size_t>(block->size-within,chunk)));
                if(self->gzip_){self->compress_block(block,within,bytes);return;}
                self->paced(bytes,false,[self,block,within,bytes]{self->stream_.expires_after(std::chrono::seconds(self->config_.timeout));
                net::async_write(self->stream_,net::buffer(block->data+within,bytes),[self,block,bytes](beast::error_code ec,std::size_t sent) {
                    self->runtime_.sent_bytes.fetch_add(sent);
                    if(ec) { self->close(); return; }
                    self->service_.transfer_activity(self->request_);
                    self->position_+=bytes; self->remaining_-=bytes; self->next_block();
                });});
            });
        });
    }
    void compress_block(std::shared_ptr<Block> block,std::size_t within,std::size_t bytes){
        if(!disk_.submit([self=shared_from_this(),block,within,bytes]{
            std::shared_ptr<std::string> wire;bool ok=true;
            try{auto data=self->gzip_->write(block->data+within,bytes,self->remaining_==bytes);wire=std::make_shared<std::string>();
                if(self->version_>=11){std::ostringstream size;size<<std::hex<<data.size()<<"\r\n";*wire=size.str()+data+"\r\n";}else *wire=std::move(data);
            }catch(...){ok=false;}
            net::post(self->stream_.get_executor(),[self,wire,bytes,ok]{if(self->closed_)return;if(!ok){self->close();return;}
                self->paced(wire->size(),false,[self,wire,bytes]{self->stream_.expires_after(std::chrono::seconds(self->config_.timeout));
                net::async_write(self->stream_,net::buffer(*wire),[self,wire,bytes](beast::error_code ec,std::size_t sent){self->runtime_.sent_bytes.fetch_add(sent);if(ec){self->close();return;}self->service_.transfer_activity(self->request_);self->position_+=bytes;self->remaining_-=bytes;self->next_block();});});
            });
        }))close();
    }
    void finish() {
        gzip_.reset();lease_.reset();traffic_lease_.reset();upload_pacing_=0; upload_.reset();incoming_.reset(); std::vector<unsigned char>().swap(upload_buffer_); request_=Request{}; response_=Response{}; serializer_.reset(); header_.reset(); parser_.reset();
        if(keep_alive_) read(); else drain_close();
    }
    void drain_close() {
        if(closed_)return;
        if(!draining_){draining_=true;drain_deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(1);}
        if(stream_.secure()&&!tls_notified_){
            tls_notified_=true;stream_.expires_at(drain_deadline_);
            stream_.async_shutdown([self=shared_from_this()](beast::error_code ec,std::size_t){if(ec)self->close();else self->drain_close();});return;
        }
        beast::error_code ignored;stream_.socket().shutdown(tcp::socket::shutdown_send,ignored);
        upload_buffer_.resize(4096);stream_.expires_at(drain_deadline_);
        stream_.lowest_layer().async_read_some(net::buffer(upload_buffer_),[self=shared_from_this()](beast::error_code error,std::size_t bytes){
            self->drained_+=bytes;if(error || self->drained_>=65536)self->close();else self->drain_close();
        });
    }
    void close() {
        if(closed_) return;
        closed_=true; beast::error_code ignored;
        release_handshake();throttle_.cancel();traffic_lease_.reset();
        if(pending_cancel_){pending_cancel_();pending_cancel_={};}
        stream_.socket().cancel(ignored); stream_.socket().shutdown(tcp::socket::shutdown_both,ignored); stream_.socket().close(ignored);
    }
    HttpTransport stream_; net::steady_timer throttle_; beast::flat_buffer buffer_;
    std::shared_ptr<void> traffic_lease_;
    Service& service_; WorkerPool& workers_; WorkerPool& disk_; RuntimeStats& runtime_; const Config& config_;
    std::optional<http::request_parser<http::buffer_body>> parser_;
    std::shared_ptr<IpTracker> tracker_;std::string peer_,authority_;bool local_admin_=false,local_device_=false,application_listener_=false,control_channel_=false;
    Request request_;std::shared_ptr<Upload> upload_;std::shared_ptr<Block> incoming_;std::vector<unsigned char> upload_buffer_;
    std::optional<http::response<http::empty_body>> header_;
    std::unique_ptr<http::response_serializer<http::empty_body>> serializer_;
    std::shared_ptr<ActiveRequest> lease_; Response response_;
    unsigned version_=11; bool keep_alive_=false,head_=false,closed_=false;
    std::size_t body_position_=0,upload_capacity_=0,upload_pacing_=0;
    std::uint64_t position_=0,remaining_=0;std::size_t drained_=0;
    bool draining_=false;std::chrono::steady_clock::time_point drain_deadline_;
    bool handshaking_=false,tls_notified_=false;
    std::unique_ptr<GzipEncoder> gzip_;
    std::function<void()> pending_cancel_;std::size_t control_limit_=65536;
};
class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(net::io_context& context,Service& service,WorkerPool& workers,WorkerPool& disk,RuntimeStats& runtime,const Config& config,std::shared_ptr<TlsContext> tls,bool control=false)
        :context_(context),acceptor_(net::make_strand(context)),retry_(context),service_(service),workers_(workers),disk_(disk),runtime_(runtime),config_(config),tracker_(std::make_shared<IpTracker>(config.ip_limit)),tls_(std::move(tls)) {
        for(const auto& adapter:interface_addresses())local_.push_back(adapter.address);
        control_=control;
        const auto address=net::ip::make_address(control?"127.0.0.1":config.bind); const tcp::endpoint endpoint(address,control?config.workspace_control_port:config.port);
        acceptor_.open(endpoint.protocol()); acceptor_.set_option(net::socket_base::reuse_address(true));
        if(address.is_v6()) acceptor_.set_option(net::ip::v6_only(true));
        acceptor_.bind(endpoint); acceptor_.listen(net::socket_base::max_listen_connections);
    }
    unsigned port()const{return acceptor_.local_endpoint().port();}
    void start() { accept(); }
    void stop() { beast::error_code ignored; acceptor_.close(ignored); retry_.cancel(); }
private:
    void accept() {
        acceptor_.async_accept(net::make_strand(context_),[self=shared_from_this()](beast::error_code error,tcp::socket socket) {
            if(error) {
                if(error==net::error::operation_aborted) return;
                self->retry_.expires_after(std::chrono::milliseconds(100)); self->retry_.async_wait([self](beast::error_code ec){if(!ec)self->accept();}); return;
            }
            if(self->runtime_.connections.fetch_add(1)>=self->config_.max_connections) {
                self->runtime_.connections.fetch_sub(1); self->runtime_.rejected.fetch_add(1); beast::error_code ignored; socket.close(ignored);
            } else {
                beast::error_code ec;const auto remote=socket.remote_endpoint(ec);
                const auto ip=ec?std::string():remote.address().to_string();
                if(ec || !self->tracker_->acquire(ip,self->service_.settings()->ip_limit)){self->runtime_.connections.fetch_sub(1);self->runtime_.rejected.fetch_add(1);socket.close(ec);}
                else std::make_shared<Session>(std::move(socket),self->service_,self->workers_,self->disk_,self->runtime_,self->config_,self->tracker_,self->local_,self->tls_,self->control_)->start();
            }
            self->accept();
        });
    }
    net::io_context& context_; tcp::acceptor acceptor_; net::steady_timer retry_;
    Service& service_; WorkerPool& workers_; WorkerPool& disk_; RuntimeStats& runtime_; const Config& config_;
    std::shared_ptr<IpTracker> tracker_;std::vector<std::string> local_;
    std::shared_ptr<TlsContext> tls_;bool control_=false;
};
std::string url_host(const std::string& address) {
    if(address.find(':')==std::string::npos) return address;
    std::string escaped; for(char c:address) escaped+=c=='%'?"%25":std::string(1,c);
    return "["+escaped+"]";
}
}
int run_server(const Config& config) {
    auto tls=config.https?std::make_shared<TlsContext>(config):std::shared_ptr<TlsContext>();
    RuntimeStats runtime; net::io_context context(static_cast<int>(config.io_threads));
    WorkerPool workers(config.file_threads,std::max<std::size_t>(64,config.max_transfers*2),config.adaptive_threads&&!config.fixed_file?config.file_max:config.file_threads,config.thread_idle);
    WorkerPool metadata(config.metadata_threads,std::max<std::size_t>(64,config.max_transfers*2),config.adaptive_threads&&!config.fixed_metadata?config.metadata_max:config.metadata_threads,config.thread_idle);
    Service service(config,workers,runtime);
    auto listener=std::make_shared<Listener>(context,service,metadata,workers,runtime,config,tls);
    std::shared_ptr<Listener> control;
    if(!config.workspace_app.empty())control=std::make_shared<Listener>(context,service,metadata,workers,runtime,config,tls,true);
    net::signal_set signals(context,SIGINT,SIGTERM);
    signals.async_wait([&](beast::error_code,unsigned){ listener->stop();if(control)control->stop(); context.stop(); });
    const bool wildcard=config.bind=="0.0.0.0" || config.bind=="::";
    const bool ipv6=config.bind.find(':')!=std::string::npos;
    const auto local=wildcard?(ipv6?"::1":"127.0.0.1"):config.bind;
    const auto scheme=std::string(config.https?"https://":"http://");
    const auto local_url=scheme+url_host(local)+":"+std::to_string(config.port)+"/";
    const auto control_url=control?scheme+"localhost:"+std::to_string(control->port())+"/":local_url;
    std::cout << "HttpLocalServer " HLS_VERSION "\nMode: " << config.mode << " (" << config.write_mode << ")\nRoot: " << config.root.u8string()
        << "\nListen: " << config.bind << ':' << config.port << "\nLocal: " << local_url << '\n';
    for(const auto& adapter:interface_addresses()) {
        const bool address_v6=adapter.address.find(':')!=std::string::npos;
        if((wildcard && address_v6==ipv6) || adapter.address==config.bind)
            std::cout << "  " << adapter.name << ": " << scheme << url_host(adapter.address) << ':' << config.port << "/\n";
    }
    if(tls)std::cout<<"HTTPS: "<<tls->description()<<"\n";
    const auto urls=site_urls(config);if(!urls.empty())std::cout<<"Recommended: "<<urls.front().url<<"\n";
    print_cli_qr(config,std::cout);
    if(config.p2p)std::cout<<"PCDN: default senders, explicit P2P downloads; "<<config.p2p_peers<<" peers/page, "<<config.p2p_cache_mb<<" MiB total page cache, "<<config.p2p_upload_kbps<<" KiB/s upload, "<<config.p2p_seed_seconds<<"s idle cache expiry\n";
    std::cout << "Cache: " << (config.cache_bytes>>20) << " MiB maximum, idle " << config.cache_idle << "s, half-life " << config.heat_half_life
        << "s\nPress Ctrl+C to stop.\n" << std::flush;
    if(control)std::cout<<"Host control: "<<control_url<<"_hls/\n"<<std::flush;
    if(config.mcp)std::cout<<"MCP: "<<control_url<<"mcp\nMCP Bearer token: "<<service.mcp_token()<<"\n"<<std::flush;
    listener->start();if(control)control->start();service.start();
    struct Runner {std::thread thread;bool finished=false;};
    std::mutex runner_mutex;std::vector<std::shared_ptr<Runner>> runners;
    unsigned live=0,desired=config.io_threads;
    const auto maximum=config.adaptive_threads&&!config.fixed_io?std::max(config.io_threads,config.io_max):config.io_threads;
    auto spawn=[&]{
        auto runner=std::make_shared<Runner>();++live;runtime.io_threads.store(live);runners.push_back(runner);
        runner->thread=std::thread([&,runner]{
            auto excess_since=std::chrono::steady_clock::now();
            bool retired=false;
            while(!context.stopped()){
                context.run_one_for(std::chrono::milliseconds(200));
                const auto now=std::chrono::steady_clock::now();
                std::lock_guard<std::mutex> lock(runner_mutex);
                if(live<=desired)excess_since=now;
                else if(now-excess_since>=std::chrono::seconds(config.thread_idle)){--live;runtime.io_threads.store(live);retired=true;break;}
            }
            std::lock_guard<std::mutex> lock(runner_mutex);if(!retired)--live;runtime.io_threads.store(live);runner->finished=true;
        });
    };
    {std::lock_guard<std::mutex> lock(runner_mutex);for(unsigned i=0;i<config.io_threads;++i)spawn();}
    if(config.open_browser)open_browser(local_url);
    while(!context.stopped()){
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        runtime.file_threads.store(workers.count());runtime.metadata_threads.store(metadata.count());
        {std::lock_guard<std::mutex> lock(runner_mutex);
            desired=std::min(maximum,config.io_threads+static_cast<unsigned>(runtime.active.load()/8));
            for(auto it=runners.begin();it!=runners.end();)if((*it)->finished){(*it)->thread.join();it=runners.erase(it);}else ++it;
            if(live<desired)spawn();
        }
        try{service.maintain();}catch(const std::exception&){}
    }
    for(auto& runner:runners)runner->thread.join();
    service.stop(); metadata.stop(); workers.stop();
    std::cout << "Stopped. Requests: " << runtime.requests.load() << ", sent bytes: " << runtime.sent_bytes.load() << '\n';
    return 0;
}
}
