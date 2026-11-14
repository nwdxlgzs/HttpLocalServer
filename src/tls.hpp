// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include <boost/asio.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hls {
// An empty context preserves the original plain TCP transport.
class TlsContext {
public:
    explicit TlsContext(const Config& config);
    ~TlsContext();
    TlsContext(const TlsContext&)=delete;
    std::string description() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class HttpTransport;
};
class HttpTransport {
public:
    using executor_type=boost::beast::tcp_stream::executor_type;
    using lowest_layer_type=boost::beast::tcp_stream;
    HttpTransport(boost::asio::ip::tcp::socket socket,std::shared_ptr<TlsContext> context);
    ~HttpTransport();
    executor_type get_executor() noexcept {return stream_.get_executor();}
    lowest_layer_type& lowest_layer() noexcept {return stream_;}
    auto& socket() noexcept {return stream_.socket();}
    bool secure() const noexcept {return !!context_;}
    template<class Duration> void expires_after(Duration value){stream_.expires_after(value);}
    void expires_at(std::chrono::steady_clock::time_point value){stream_.expires_at(value);}
    template<class CompletionToken> auto async_handshake(CompletionToken&& token){
        return initiate(Kind::handshake,{}, {},std::forward<CompletionToken>(token));
    }
    template<class CompletionToken> auto async_shutdown(CompletionToken&& token){
        return initiate(Kind::shutdown,{}, {},std::forward<CompletionToken>(token));
    }
    template<class Buffers,class CompletionToken> auto async_read_some(const Buffers& buffers,CompletionToken&& token){
        boost::asio::mutable_buffer first;
        for(auto i=boost::asio::buffer_sequence_begin(buffers),end=boost::asio::buffer_sequence_end(buffers);i!=end;++i)
            if(boost::asio::buffer_size(*i)){first=*i;break;}
        return initiate(Kind::read,first,{},std::forward<CompletionToken>(token));
    }
    template<class Buffers,class CompletionToken> auto async_write_some(const Buffers& buffers,CompletionToken&& token){
        boost::asio::const_buffer first;
        for(auto i=boost::asio::buffer_sequence_begin(buffers),end=boost::asio::buffer_sequence_end(buffers);i!=end;++i)
            if(boost::asio::buffer_size(*i)){first=*i;break;}
        return initiate(Kind::write,{},first,std::forward<CompletionToken>(token));
    }
private:
    enum class Kind {handshake,read,write,shutdown};
    using Done=std::function<void(boost::system::error_code,std::size_t)>;
    template<class CompletionToken> auto initiate(Kind kind,boost::asio::mutable_buffer output,boost::asio::const_buffer input,CompletionToken&& token){
        return boost::asio::async_initiate<CompletionToken,void(boost::system::error_code,std::size_t)>(
            [this,kind,output,input](auto&& completion){
                using Handler=std::decay_t<decltype(completion)>;
                auto handler=std::make_shared<Handler>(std::forward<decltype(completion)>(completion));
                // Never invoke a completion inline, even for buffered plaintext.
                boost::asio::post(get_executor(),[this,kind,output,input,handler]{
                    begin(kind,output,input,[handler](boost::system::error_code ec,std::size_t bytes){(*handler)(ec,bytes);});
                });
            },token);
    }
    void begin(Kind kind,boost::asio::mutable_buffer output,boost::asio::const_buffer input,Done done);
    struct Engine;
    struct Operation;
    boost::beast::tcp_stream stream_;
    std::shared_ptr<TlsContext> context_;
    std::unique_ptr<Engine> engine_;
    bool busy_=false;
};
}
