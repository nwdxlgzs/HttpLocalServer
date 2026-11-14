// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "tls.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#ifdef HLS_ENABLE_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/oid.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ticket.h>
#include <mbedtls/threading.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#endif

namespace hls {
namespace net=boost::asio;
using Error=boost::system::error_code;
#ifdef HLS_ENABLE_TLS
namespace {
void mutex_init(mbedtls_threading_mutex_t* value){try{value->handle=new std::mutex;}catch(...){value->handle=nullptr;}}
void mutex_free(mbedtls_threading_mutex_t* value){delete static_cast<std::mutex*>(value->handle);value->handle=nullptr;}
int mutex_lock(mbedtls_threading_mutex_t* value){if(!value->handle)return MBEDTLS_ERR_THREADING_MUTEX_ERROR;try{static_cast<std::mutex*>(value->handle)->lock();return 0;}catch(...){return MBEDTLS_ERR_THREADING_MUTEX_ERROR;}}
int mutex_unlock(mbedtls_threading_mutex_t* value){if(!value->handle)return MBEDTLS_ERR_THREADING_MUTEX_ERROR;static_cast<std::mutex*>(value->handle)->unlock();return 0;}
struct GlobalTLS {
    GlobalTLS(){mbedtls_threading_set_alt(mutex_init,mutex_free,mutex_lock,mutex_unlock);if(psa_crypto_init()!=PSA_SUCCESS){mbedtls_psa_crypto_free();mbedtls_threading_free_alt();throw std::runtime_error("Cannot initialize TLS cryptography");}}
    ~GlobalTLS(){mbedtls_psa_crypto_free();mbedtls_threading_free_alt();}
};
class TlsErrors final:public boost::system::error_category {
public:
    const char* name() const noexcept override{return "mbedTLS";}
    std::string message(int value) const override{char text[256];mbedtls_strerror(-value,text,sizeof(text));return text;}
};
Error tls_error(int value){static const TlsErrors category;return {-value,category};}
void checked(int value,const char* action){if(value!=0)throw std::runtime_error(std::string(action)+": "+tls_error(value).message());}
struct SecretBytes {
    std::vector<unsigned char> bytes;
    ~SecretBytes(){if(!bytes.empty())mbedtls_platform_zeroize(bytes.data(),bytes.size());}
};
void read_credential(const std::filesystem::path& path,SecretBytes& result){
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open TLS certificate or private key");
    const auto length=input.tellg();if(length<=0||length>1024*1024)throw std::runtime_error("TLS credential must be between 1 byte and 1 MiB");
    result.bytes.resize(static_cast<std::size_t>(length)+1);input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(result.bytes.data()),length))throw std::runtime_error("Cannot read TLS credential");
    if(result.bytes.size()<12||std::memcmp(result.bytes.data(),"-----BEGIN ",11)!=0)result.bytes.pop_back();
}
std::string certificate_time(std::time_t time){
    std::tm value{};
#ifdef _WIN32
    gmtime_s(&value,&time);
#else
    gmtime_r(&time,&value);
#endif
    char result[16];if(!std::strftime(result,sizeof(result),"%Y%m%d%H%M%S",&value))throw std::runtime_error("Cannot format TLS validity");return result;
}
}
struct TlsContext::Impl {
    mbedtls_ssl_config config;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_ssl_ticket_context tickets;
    std::string fingerprint;
    bool temporary=false;
    Impl(){
        static GlobalTLS runtime;(void)runtime;
        mbedtls_ssl_config_init(&config);mbedtls_x509_crt_init(&certificate);mbedtls_pk_init(&key);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&random);mbedtls_ssl_ticket_init(&tickets);
    }
    ~Impl(){mbedtls_ssl_config_free(&config);mbedtls_ssl_ticket_free(&tickets);mbedtls_x509_crt_free(&certificate);mbedtls_pk_free(&key);mbedtls_ctr_drbg_free(&random);mbedtls_entropy_free(&entropy);}
    void self_signed(const Config& options){
        temporary=true;
        checked(mbedtls_pk_setup(&key,mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)),"Create TLS key");
        checked(mbedtls_rsa_gen_key(mbedtls_pk_rsa(key),mbedtls_ctr_drbg_random,&random,2048,65537),"Generate TLS key");
        struct Writer {mbedtls_x509write_cert value;Writer(){mbedtls_x509write_crt_init(&value);}~Writer(){mbedtls_x509write_crt_free(&value);}} writer;
        auto* cert=&writer.value;mbedtls_x509write_crt_set_version(cert,MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(cert,MBEDTLS_MD_SHA256);mbedtls_x509write_crt_set_subject_key(cert,&key);mbedtls_x509write_crt_set_issuer_key(cert,&key);
        checked(mbedtls_x509write_crt_set_subject_name(cert,"CN=HttpLocalServer,O=HttpLocalServer"),"Set TLS subject");
        checked(mbedtls_x509write_crt_set_issuer_name(cert,"CN=HttpLocalServer,O=HttpLocalServer"),"Set TLS issuer");
        unsigned char serial[16];checked(mbedtls_ctr_drbg_random(&random,serial,sizeof(serial)),"Create TLS serial");serial[0]&=0x7f;serial[0]|=1;
        checked(mbedtls_x509write_crt_set_serial_raw(cert,serial,sizeof(serial)),"Set TLS serial");
        const auto now=std::time(nullptr);const auto before=certificate_time(now-86400),after=certificate_time(now+30*86400);
        checked(mbedtls_x509write_crt_set_validity(cert,before.c_str(),after.c_str()),"Set TLS validity");
        checked(mbedtls_x509write_crt_set_basic_constraints(cert,0,-1),"Set TLS constraints");
        // A non-CA self-signed leaf with only digitalSignature is rejected as
        // an explicit trust anchor by older OpenSSL clients. BasicConstraints
        // still forbids issuing certificates, and EKU restricts server use.
        checked(mbedtls_x509write_crt_set_subject_key_identifier(cert),"Set TLS subject identifier");
        checked(mbedtls_x509write_crt_set_authority_key_identifier(cert),"Set TLS issuer identifier");
        mbedtls_asn1_sequence usage{};usage.buf.tag=MBEDTLS_ASN1_OID;usage.buf.p=reinterpret_cast<unsigned char*>(const_cast<char*>(MBEDTLS_OID_SERVER_AUTH));usage.buf.len=MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
        checked(mbedtls_x509write_crt_set_ext_key_usage(cert,&usage),"Set TLS server usage");
        std::vector<std::string> addresses={"127.0.0.1","::1",options.bind};
        for(const auto& adapter:interface_addresses())if(addresses.size()<64)addresses.push_back(adapter.address);
        std::vector<std::vector<unsigned char>> values;values.push_back({'l','o','c','a','l','h','o','s','t'});
        for(const auto& name:addresses){Error ec;const auto ip=net::ip::make_address(name,ec);if(ec||ip.is_unspecified()||ip.is_multicast())continue;
            std::vector<unsigned char> value;if(ip.is_v4()){const auto bytes=ip.to_v4().to_bytes();value.assign(bytes.begin(),bytes.end());}else{const auto bytes=ip.to_v6().to_bytes();value.assign(bytes.begin(),bytes.end());}
            if(std::find(values.begin(),values.end(),value)==values.end())values.push_back(std::move(value));
        }
        std::vector<mbedtls_x509_san_list> sans(values.size());
        for(std::size_t i=0;i<values.size();++i){sans[i].node.type=i?MBEDTLS_X509_SAN_IP_ADDRESS:MBEDTLS_X509_SAN_DNS_NAME;sans[i].node.san.unstructured_name.p=values[i].data();sans[i].node.san.unstructured_name.len=values[i].size();sans[i].next=i+1<values.size()?&sans[i+1]:nullptr;}
        checked(mbedtls_x509write_crt_set_subject_alternative_name(cert,sans.data()),"Set TLS address names");
        unsigned char der[16384];const auto length=mbedtls_x509write_crt_der(cert,der,sizeof(der),mbedtls_ctr_drbg_random,&random);
        if(length<0)checked(length,"Create self-signed TLS certificate");
        checked(mbedtls_x509_crt_parse_der(&certificate,der+sizeof(der)-length,static_cast<std::size_t>(length)),"Load self-signed certificate");
    }
};
TlsContext::TlsContext(const Config& options):impl_(std::make_unique<Impl>()){
    auto& state=*impl_;const unsigned char personal[]="HttpLocalServer HTTPS";
    checked(mbedtls_ctr_drbg_seed(&state.random,mbedtls_entropy_func,&state.entropy,personal,sizeof(personal)-1),"Seed TLS random source");
    if(options.tls_self_signed)state.self_signed(options);
    else{
        SecretBytes certificate,key;read_credential(options.tls_cert,certificate);read_credential(options.tls_key,key);
        checked(mbedtls_x509_crt_parse(&state.certificate,certificate.bytes.data(),certificate.bytes.size()),"Parse TLS certificate chain");
        checked(mbedtls_pk_parse_key(&state.key,key.bytes.data(),key.bytes.size(),nullptr,0,mbedtls_ctr_drbg_random,&state.random),"Parse unencrypted TLS private key");
    }
    if(mbedtls_pk_get_bitlen(&state.key)<(mbedtls_pk_can_do(&state.key,MBEDTLS_PK_RSA)?2048u:256u))throw std::runtime_error("TLS key is too small (RSA 2048 / EC 256 minimum)");
    checked(mbedtls_pk_check_pair(&state.certificate.pk,&state.key,mbedtls_ctr_drbg_random,&state.random),"TLS certificate/private key mismatch");
    checked(mbedtls_ssl_config_defaults(&state.config,MBEDTLS_SSL_IS_SERVER,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT),"Configure TLS server");
    mbedtls_ssl_conf_rng(&state.config,mbedtls_ctr_drbg_random,&state.random);
    mbedtls_ssl_conf_authmode(&state.config,MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_min_tls_version(&state.config,MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&state.config,MBEDTLS_SSL_VERSION_TLS1_3);
    static const int suites[]={MBEDTLS_TLS1_3_AES_128_GCM_SHA256,MBEDTLS_TLS1_3_AES_256_GCM_SHA384,MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,0};
    mbedtls_ssl_conf_ciphersuites(&state.config,suites);
    mbedtls_ssl_conf_renegotiation(&state.config,MBEDTLS_SSL_RENEGOTIATION_DISABLED);
    checked(mbedtls_ssl_ticket_setup(&state.tickets,mbedtls_ctr_drbg_random,&state.random,MBEDTLS_CIPHER_AES_256_GCM,3600),"Configure TLS tickets");
    mbedtls_ssl_conf_session_tickets_cb(&state.config,mbedtls_ssl_ticket_write,mbedtls_ssl_ticket_parse,&state.tickets);
    mbedtls_ssl_conf_new_session_tickets(&state.config,1);
    mbedtls_ssl_conf_tls13_key_exchange_modes(&state.config,MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL|MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL);
    static const char* protocols[]={"http/1.1",nullptr};checked(mbedtls_ssl_conf_alpn_protocols(&state.config,protocols),"Configure HTTPS ALPN");
    checked(mbedtls_ssl_conf_own_cert(&state.config,&state.certificate,&state.key),"Configure TLS certificate");
    unsigned char digest[32];checked(mbedtls_sha256(state.certificate.raw.p,state.certificate.raw.len,digest,0),"Hash TLS certificate");
    std::ostringstream text;text<<std::hex<<std::setfill('0');for(std::size_t i=0;i<sizeof(digest);++i){if(i)text<<':';text<<std::setw(2)<<unsigned(digest[i]);}state.fingerprint=text.str();
}
TlsContext::~TlsContext()=default;
std::string TlsContext::description() const{return std::string("mbedTLS 3.6.7; TLS 1.2/1.3; ")+(impl_->temporary?"temporary self-signed certificate (new each run)":"configured certificate")+"\nCertificate SHA-256: "+impl_->fingerprint;}

struct HttpTransport::Engine {
    mbedtls_ssl_context ssl;
    std::array<unsigned char,16384> incoming{};
    std::size_t read_at=0,read_size=0;
    std::vector<unsigned char> outgoing;
    explicit Engine(TlsContext::Impl& context){
        mbedtls_ssl_init(&ssl);const auto code=mbedtls_ssl_setup(&ssl,&context.config);
        if(code){mbedtls_ssl_free(&ssl);checked(code,"Allocate TLS session");}
        mbedtls_ssl_set_bio(&ssl,this,send,receive,nullptr);
    }
    ~Engine(){mbedtls_ssl_free(&ssl);}
    static int send(void* raw,const unsigned char* bytes,std::size_t size){
        auto& engine=*static_cast<Engine*>(raw);
        if(size>65536-engine.outgoing.size())return MBEDTLS_ERR_SSL_WANT_WRITE;
        try{engine.outgoing.insert(engine.outgoing.end(),bytes,bytes+size);}catch(...){return MBEDTLS_ERR_SSL_ALLOC_FAILED;}
        return static_cast<int>(size);
    }
    static int receive(void* raw,unsigned char* bytes,std::size_t size){
        auto& engine=*static_cast<Engine*>(raw);const auto count=std::min(size,engine.read_size-engine.read_at);
        if(!count)return MBEDTLS_ERR_SSL_WANT_READ;
        std::memcpy(bytes,engine.incoming.data()+engine.read_at,count);engine.read_at+=count;return static_cast<int>(count);
    }
};
struct HttpTransport::Operation:std::enable_shared_from_this<Operation> {
    HttpTransport& owner;Kind kind;net::mutable_buffer output;net::const_buffer input;Done done;
    bool completed=false;int result=0;
    Operation(HttpTransport& value,Kind operation,net::mutable_buffer out,net::const_buffer in,Done callback):owner(value),kind(operation),output(out),input(in),done(std::move(callback)){}
    void finish(Error error,std::size_t bytes=0){owner.busy_=false;auto callback=std::move(done);callback(error,bytes);}
    void step(){
        auto& engine=*owner.engine_;
        if(!completed){
            if(kind==Kind::handshake)result=mbedtls_ssl_handshake(&engine.ssl);
            else if(kind==Kind::shutdown)result=mbedtls_ssl_close_notify(&engine.ssl);
            else if(kind==Kind::read)result=mbedtls_ssl_read(&engine.ssl,static_cast<unsigned char*>(output.data()),std::min<std::size_t>(output.size(),16384));
            else result=mbedtls_ssl_write(&engine.ssl,static_cast<const unsigned char*>(input.data()),std::min<std::size_t>(input.size(),16384));
            completed=result!=MBEDTLS_ERR_SSL_WANT_READ&&result!=MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        if(!engine.outgoing.empty()){
            net::async_write(owner.stream_,net::buffer(engine.outgoing),[self=shared_from_this()](Error ec,std::size_t){
                if(ec){self->finish(ec);return;}self->owner.engine_->outgoing.clear();self->step();
            });return;
        }
        if(completed){
            if(result==0&&(kind==Kind::handshake||kind==Kind::shutdown)){finish({});return;}
            if(result>0){finish({},static_cast<std::size_t>(result));return;}
            if(result==0||result==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY){finish(net::error::eof);return;}
            finish(tls_error(result));return;
        }
        if(result==MBEDTLS_ERR_SSL_WANT_WRITE){finish(tls_error(MBEDTLS_ERR_SSL_INTERNAL_ERROR));return;}
        // A pending TLS read consumes any remaining record bytes before asking
        // the socket for another bounded encrypted chunk.
        if(engine.read_at!=engine.read_size){finish(tls_error(MBEDTLS_ERR_SSL_INTERNAL_ERROR));return;}
        engine.read_at=engine.read_size=0;
        owner.stream_.async_read_some(net::buffer(engine.incoming),[self=shared_from_this()](Error ec,std::size_t bytes){
            if(ec){self->finish(ec==net::error::eof?tls_error(MBEDTLS_ERR_SSL_CONN_EOF):ec);return;}
            self->owner.engine_->read_size=bytes;self->step();
        });
    }
};
#else
struct TlsContext::Impl {};
struct HttpTransport::Engine {};
TlsContext::TlsContext(const Config&){throw std::runtime_error("HTTPS is not included in this build; configure with -DHLS_ENABLE_TLS=ON");}
TlsContext::~TlsContext()=default;
std::string TlsContext::description() const{return {};}
#endif
HttpTransport::HttpTransport(net::ip::tcp::socket socket,std::shared_ptr<TlsContext> context):stream_(std::move(socket)),context_(std::move(context)){}
HttpTransport::~HttpTransport()=default;
void HttpTransport::begin(Kind kind,net::mutable_buffer output,net::const_buffer input,Done done){
    if(busy_){done(net::error::already_started,0);return;}
    if(!context_){
        if(kind==Kind::read)stream_.async_read_some(output,std::move(done));
        else if(kind==Kind::write)stream_.async_write_some(input,std::move(done));
        else done({},0);
        return;
    }
#ifdef HLS_ENABLE_TLS
    if((kind==Kind::read&&!output.size())||(kind==Kind::write&&!input.size())){done({},0);return;}
    if(!engine_){try{engine_=std::make_unique<Engine>(*context_->impl_);}catch(...){done(net::error::no_memory,0);return;}}
    busy_=true;std::make_shared<Operation>(*this,kind,output,input,std::move(done))->step();
#else
    done(net::error::operation_not_supported,0);
#endif
}
}
