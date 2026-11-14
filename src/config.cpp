#include "version.hpp"
#include "config.hpp"
#include "config_store.hpp"
#include "script_policy.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <thread>
#include <vector>

namespace hls {
namespace {
std::uint64_t number(const std::string& text, std::uint64_t low, std::uint64_t high, const std::string& option) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result < low || result > high)
        throw std::runtime_error("Invalid " + option + ": " + text);
    return result;
}
}
Config parse_config(int argc, char** argv) {
    Config c;
    auto arguments=config_arguments(c,argc,argv);
    std::vector<char*> pointers;for(auto& argument:arguments)pointers.push_back(argument.data());
    argc=static_cast<int>(pointers.size());argv=pointers.data();
    const auto cpus=std::max(2u,std::thread::hardware_concurrency());
    c.io_max=std::min(16u,std::max(2u,cpus*3/4));
    c.file_max=std::min(16u,std::max(4u,cpus/2));
    c.metadata_max=std::min(8u,std::max(2u,cpus/4));
    c.io_threads=std::min(2u,c.io_max); c.file_threads=2; c.metadata_threads=1;
    for (int i = 1; i < argc; ++i) {
        std::string option = argv[i];
        if (option == "filetrans" || option == "server") { c.mode = option; continue; }
        if (option == "--help" || option == "-h") { c.help = true; continue; }
        if (option == "--licenses") { c.licenses = true; continue; }
        if (option == "--version") { c.version = true; continue; }
        if(option=="--wasm"){c.wasm=true;continue;}
        if(option=="--no-wasm"){c.wasm=false;continue;}
        if(option=="--workspace-share"){c.workspace_share=true;continue;}
        if(option=="--no-workspace-share"){c.workspace_share=false;continue;}
        if(option=="--workspace-args-clear"){c.workspace_args.clear();continue;}
        if(option=="--recycle-bin"){c.recycle=true;continue;}
        if(option=="--no-recycle-bin"){c.recycle=false;continue;}
        if(option=="--strict-host"){c.strict_host=true;continue;}
        if(option=="--show-cli-qr"){c.show_cli_qr=true;continue;}
        if(option=="--compression"){c.compression=true;continue;}
        if(option=="--no-compression"){c.compression=false;continue;}
        if(option=="--https"){c.https=true;continue;}
        if(option=="--no-https"){c.https=false;c.tls_self_signed=false;c.tls_cert.clear();c.tls_key.clear();continue;}
        if(option=="--tls-self-signed"){c.https=true;c.tls_self_signed=true;continue;}
        if(option=="--no-tls-self-signed"){c.tls_self_signed=false;continue;}
        if(option=="--p2p"){c.p2p=true;continue;}
        if(option=="--no-p2p"){c.p2p=false;continue;}
        if (option == "--open") { c.open_browser = true; continue; }
        if (option == "--no-open") { c.open_browser = false; continue; }
        if (option == "--index") { c.index = true; continue; }
        if (option == "--no-index") { c.index = false; continue; }
        if (option == "--allow-preview") { c.allow_preview = true; continue; }
        if (option == "--no-preview") { c.allow_preview = false; continue; }
        if (option == "--lazy-thumbnails") { c.thumbnails = true; continue; }
        if (option == "--no-lazy-thumbnails") { c.thumbnails = false; continue; }
        if (option == "--mcp") { c.mcp = true; continue; }
        if (option == "--no-mcp") { c.mcp = false; continue; }
        if (option == "--magika") { c.magika = true; continue; }
        if (option == "--no-magika") { c.magika = false; continue; }
        if (option == "--system-root") {c.system_root=true;c.index=false;continue;}
        if (option == "--no-system-root") {c.system_root=false;continue;}
        if (option == "--no-show-cli-qr") {c.show_cli_qr=false;continue;}
        std::string value;
        const auto eq = option.find('=');
        if (eq != std::string::npos) { value = option.substr(eq + 1); option.resize(eq); }
        else { if (++i >= argc) throw std::runtime_error("Missing value for " + option); value = argv[i]; }
        auto n = [&](std::uint64_t lo, std::uint64_t hi) { return number(value, lo, hi, option); };
        if(option=="--zip-preview"){if(value!="off"&&value!="host"&&value!="client")throw std::runtime_error("ZIP preview must be off, host or client");c.zip_preview=value;}
        else if(option=="--state-dir")c.state_dir=std::filesystem::u8path(value);
        else if(option=="--workspace-arg"){if(c.workspace_args.size()>=128||value.size()>4096)throw std::runtime_error("Workspace argument budget exceeded");c.workspace_args.push_back(value);}
        else if(option=="--workspace-access"){if(value!="lan"&&value!="host")throw std::runtime_error("Workspace access must be lan or host");c.workspace_access=value;}
        else if(option=="--workspace-allow-ip"){if(!valid_ip_rule(value))throw std::runtime_error("Invalid workspace IP/CIDR rule");if(c.workspace_allow_ips.size()>=256)throw std::runtime_error("Too many workspace IP rules");c.workspace_allow_ips.push_back(value);}
        else if(option=="--workspace-public")c.workspace_public=value;
        else if(option=="--script-fs"||option=="--workspace-fs"){if(value!="off"&&value!="readonly"&&value!="readwrite")throw std::runtime_error("Script filesystem must be off, readonly or readwrite");c.script_fs=value;}
        else if(option=="--wasm-memory-mb")c.wasm_memory_mb=static_cast<unsigned>(n(4,256));
        else if(option=="--wasm-instances")c.wasm_instances=static_cast<unsigned>(n(1,64));
        else if(option=="--wasm-instructions")c.wasm_instructions=static_cast<unsigned>(n(1000,100000000));
        else if(option=="--workspace-control-port")c.workspace_control_port=static_cast<std::uint16_t>(n(0,65535));
        else if(option=="--workspace-cache-mb")c.workspace_cache_bytes=static_cast<std::size_t>(n(0,256))<<20;
        else if(option=="--workspace-app")c.workspace_app=std::filesystem::u8path(value);
        else if(option=="--script")c.script_path=std::filesystem::u8path(value);
        else if(option=="--script-memory-mb")c.script_memory_mb=static_cast<unsigned>(n(4,256));
        else if(option=="--script-timeout-ms")c.script_timeout_ms=static_cast<unsigned>(n(1,10000));
        else if(option=="--script-concurrency")c.script_concurrency=static_cast<unsigned>(n(1,64));
        else if(option=="--default-documents"){if(value.size()>4096)throw std::runtime_error("Default documents too long");c.default_documents=value;}
        else if(option=="--spa-fallback")c.spa_fallback=value;
        else if(option=="--response-header"||option=="--error-page"){
            const auto split=value.find('=');if(split==std::string::npos||split==0||value.size()>8192)throw std::runtime_error("Expected NAME=VALUE");
            if(option=="--response-header"){
                const auto name=value.substr(0,split);auto equal=[](std::string a,std::string b){auto fold=[](unsigned char ch){return static_cast<char>(ch>='A'&&ch<='Z'?ch+32:ch);};std::transform(a.begin(),a.end(),a.begin(),fold);std::transform(b.begin(),b.end(),b.begin(),fold);return a==b;};
                for(auto it=c.response_headers.begin();it!=c.response_headers.end();)if(equal(it->first,name))it=c.response_headers.erase(it);else ++it;
                c.response_headers[name]=value.substr(split+1);
            }else c.error_pages[value.substr(0,split)]=value.substr(split+1);
        }
        else if(option=="--recycle-days")c.recycle_days=static_cast<unsigned>(n(1,3650));
        else if(option=="--recycle-mb")c.recycle_bytes=n(1,1073741824)<<20;
        else if(option=="--resume-hours")c.resume_hours=static_cast<unsigned>(n(1,168));
        else if(option=="--resume-mb")c.resume_bytes=n(1,1073741824)<<20;
        else if(option=="--ip-transfers")c.ip_transfers=static_cast<unsigned>(n(0,4096));
        else if(option=="--upload-kbps")c.upload_kbps=n(0,1073741824);
        else if(option=="--download-kbps")c.download_kbps=n(0,1073741824);
        else if(option=="--ip-upload-kbps")c.ip_upload_kbps=n(0,1073741824);
        else if(option=="--ip-download-kbps")c.ip_download_kbps=n(0,1073741824);
        else if(option=="--password"){if(value.size()>1024)throw std::runtime_error("Password is too long");c.login_password=value;}
        else if(option=="--hotlink-seconds")c.hotlink_seconds=static_cast<unsigned>(n(0,86400));
        else if (option == "--mode") c.mode = value;
        else if (option == "--bind" || option == "-b") c.bind = value;
        else if (option == "--port" || option == "-p") c.port = static_cast<std::uint16_t>(n(1, 65535));
        else if(option=="--tls-cert")c.tls_cert=std::filesystem::u8path(value);
        else if(option=="--tls-key")c.tls_key=std::filesystem::u8path(value);
        else if(option=="--tls-handshakes")c.tls_handshakes=static_cast<unsigned>(n(1,4096));
        else if(option=="--tls-handshake-timeout")c.tls_handshake_timeout=static_cast<unsigned>(n(1,300));
        else if(option=="--p2p-peers")c.p2p_peers=static_cast<unsigned>(n(1,8));
        else if(option=="--p2p-cache-mb")c.p2p_cache_mb=static_cast<unsigned>(n(0,64));
        else if(option=="--p2p-upload-kbps")c.p2p_upload_kbps=static_cast<unsigned>(n(0,1048576));
        else if(option=="--p2p-cache-seconds"||option=="--p2p-seed-seconds")c.p2p_seed_seconds=static_cast<unsigned>(n(1,86400));
        else if (option == "--root" || option == "-r") c.root = std::filesystem::u8path(value);
        else if(option=="--mount"||option=="--mount-rw"){
            const auto split=value.find('=');if(split==std::string::npos||!split||split+1==value.size())throw std::runtime_error("Mount syntax: --mount /name=/physical/path");
            const auto logical=value.substr(0,split);c.mounts.erase(std::remove_if(c.mounts.begin(),c.mounts.end(),[&](const auto& mount){return mount.path==logical;}),c.mounts.end());
            if(c.mounts.size()>=64)throw std::runtime_error("At most 64 mounts are supported");
            c.mounts.push_back({value.substr(0,split),std::filesystem::u8path(value.substr(split+1)),option=="--mount-rw"});
        }
        else if(option=="--mount-root"){if(std::find(c.root_mounts.begin(),c.root_mounts.end(),value)==c.root_mounts.end())c.root_mounts.push_back(value);}
        else if(option=="--mount-volumes"){if(std::find(c.volume_mounts.begin(),c.volume_mounts.end(),value)==c.volume_mounts.end())c.volume_mounts.push_back(value);}
        else if (option == "--io-threads") { c.io_threads=static_cast<unsigned>(n(1,128)); c.fixed_io=true; }
        else if (option == "--file-threads") { c.file_threads=static_cast<unsigned>(n(1,64)); c.fixed_file=true; }
        else if (option == "--metadata-threads") { c.metadata_threads=static_cast<unsigned>(n(1,32)); c.fixed_metadata=true; }
        else if (option == "--io-max") c.io_max=static_cast<unsigned>(n(1,128));
        else if (option == "--file-max") c.file_max=static_cast<unsigned>(n(1,64));
        else if (option == "--metadata-max") c.metadata_max=static_cast<unsigned>(n(1,32));
        else if (option == "--thread-idle") c.thread_idle=static_cast<unsigned>(n(1,3600));
        else if (option == "--threads") { if(value!="auto" && value!="fixed") throw std::runtime_error("--threads must be auto or fixed"); c.adaptive_threads=value=="auto"; }
        else if (option == "--write-mode") c.write_mode=value;
        else if (option == "--attention-click") c.attention_click=value;
        else if (option == "--ip-limit") c.ip_limit=n(0,1000000);
        else if (option == "--owner-limit") c.owner_limit=n(0,1000000);
        else if (option == "--max-upload-mb") c.max_upload_bytes=n(1,1048576)*(1ull<<20);
        else if(option=="--ip-upload-mb")c.ip_upload_bytes=n(0,1073741824)*(1ull<<20);
        else if(option=="--ip-upload-gb")c.ip_upload_bytes=n(0,1048576)*(1ull<<30);
        else if(option=="--ip-upload-limit"){
            const auto split=value.rfind('=');if(split==std::string::npos)throw std::runtime_error("Use --ip-upload-limit IP=10GB or IP=512MB");
            const auto amount=value.substr(split+1);std::uint64_t unit=1ull<<20;auto digits=amount;
            if(amount.size()>=2){const auto suffix=amount.substr(amount.size()-2);if(suffix=="GB"||suffix=="gb"){unit=1ull<<30;digits.resize(digits.size()-2);}else if(suffix=="MB"||suffix=="mb")digits.resize(digits.size()-2);}
            if(c.ip_upload_overrides.size()>=256)throw std::runtime_error("At most 256 IP allowance overrides");
            c.ip_upload_overrides[value.substr(0,split)]=number(digits,0,(1ull<<50)/unit,option)*unit;
        }
        else if (option == "--magika-threads") c.magika_threads=static_cast<unsigned>(n(1,4));
        else if (option == "--cpu-kernel") c.cpu_kernel=value;
        else if (option == "--max-connections") c.max_connections = n(1, 1000000);
        else if (option == "--max-transfers") c.max_transfers = n(1, 65536);
        else if (option == "--cache-mb") c.cache_bytes = n(0, 1048576) * (1ull << 20);
        else if (option == "--buffer-mb") c.buffer_bytes = n(1, 65536) * (1ull << 20);
        else if (option == "--block-kb") c.block_bytes = n(4, 1024) * 1024;
        else if (option == "--cache-idle") c.cache_idle = static_cast<unsigned>(n(1, 86400));
        else if (option == "--heat-half-life") c.heat_half_life = static_cast<unsigned>(n(1, 86400));
        else if (option == "--index-interval") c.index_interval = static_cast<unsigned>(n(1, 86400));
        else if (option == "--index-limit") c.index_limit = n(1, 10000000);
        else if (option == "--directory-limit") c.directory_limit = n(1, 1000000);
        else if (option == "--timeout") c.timeout = static_cast<unsigned>(n(1, 3600));
        else throw std::runtime_error("Unknown option: " + option);
    }
    if(!c.workspace_app.empty()&&(c.mode!="server"||!c.script_path.empty()))throw std::runtime_error("--workspace-app requires Server mode and cannot be combined with --script");
    if(!c.workspace_app.empty()&&c.workspace_control_port&&c.workspace_control_port==c.port)throw std::runtime_error("Workspace control port must differ from application port");
    if (c.mode != "server" && c.mode != "filetrans") throw std::runtime_error("Mode must be server or filetrans");
    if(!c.help&&!c.version&&!c.licenses){
        if(!c.https&&(!c.tls_cert.empty()||!c.tls_key.empty()))throw std::runtime_error("--tls-cert/--tls-key require --https");
        if(c.tls_self_signed&&(!c.tls_cert.empty()||!c.tls_key.empty()))throw std::runtime_error("Choose --tls-self-signed OR --tls-cert with --tls-key");
        if(c.https&&!c.tls_self_signed&&(c.tls_cert.empty()||c.tls_key.empty()))throw std::runtime_error("HTTPS requires --tls-cert CERT --tls-key KEY, or --tls-self-signed");
    }


    if(c.write_mode!="readonly" && c.write_mode!="isolated" && c.write_mode!="shared") throw std::runtime_error("--write-mode must be readonly, isolated or shared");
    if(c.attention_click!="locate" && c.attention_click!="download") throw std::runtime_error("--attention-click must be locate or download");
    if(!c.fixed_io) c.io_threads=c.adaptive_threads?std::min(2u,c.io_max):c.io_max;
    if(!c.fixed_file) c.file_threads=c.adaptive_threads?std::min(2u,c.file_max):c.file_max;
    if(!c.fixed_metadata) c.metadata_threads=c.adaptive_threads?1:c.metadata_max;
    if (c.buffer_bytes < c.block_bytes) throw std::runtime_error("Buffer budget must hold at least one block");
    if (c.buffer_bytes < 2*c.max_transfers*c.block_bytes)
        throw std::runtime_error("--buffer-mb must reserve two blocks per active transfer; increase it or lower --max-transfers / --block-kb");
    return c;
}
std::string help_text() {
    return "HttpLocalServer " HLS_VERSION R"( - LAN HTTP file server
Usage: HttpLocalServer [filetrans|server] [options]

  --state-dir PATH       Private durable state/spool directory (default: beside config/root)
  --recycle-bin          Recycle deletes (default: off); --no-recycle-bin disables
  --recycle-days N       Retention days (7); --recycle-mb N capacity MiB (1024)
  --resume-hours N       Incomplete upload expiry hours (24, max 168)
  --resume-mb N          Aggregate resumable upload reserved MiB (65536)
  --upload-kbps N        Global receive KiB/s (0=unlimited)
  --download-kbps N      Global send KiB/s (0=unlimited)
  --ip-upload-kbps N     Receive KiB/s per IP (0=unlimited)
  --ip-download-kbps N   Send KiB/s per IP (0=unlimited)
  --ip-transfers N       Concurrent transfers per IP (0=unlimited; global ceiling still applies)
  --default-documents CSV  Server index candidates (index.html,index.htm)
  --spa-fallback PATH    Server HTML fallback for extensionless routes
  --response-header K=V Add a safe response header (repeatable)
  --error-page CODE=PATH Server custom HTML error document (repeatable)
  --script PATH          Trusted QuickJS-NG hooks (no OS/module bindings)
  --script-memory-mb N   JS memory ceiling per invocation (16 MiB)
  --script-timeout-ms N  JS deadline per invocation (50 ms)
  --script-concurrency N Concurrent isolated JS invocations (4)
  --workspace-cache-mb N Retained ZIP static asset cache (16 MiB; 0=off)
  --config PATH          JSON configuration (default: ./config.json); CLI wins
  --no-config            Disable JSON loading and persistence for this process

  --root, -r PATH         Shared directory (default: current directory)
  --system-root          Share system root; Windows shows C, D, E... at /
  --mount /NAME=PATH      Mount an existing host directory or partition, read-only
  --mount-rw /NAME=PATH   Mount with write capability (write-mode still applies)
  --mount-root /NAME     Mount the system root below /NAME (read-only)
  --mount-volumes /NAME  Mount OS volumes below /NAME (read-only)
  --bind, -b IP           Listen address (default: 0.0.0.0; IPv6 supported)
  --port, -p PORT         Listen port (default: 8000)
  --https                Use HTTPS on --port instead of HTTP (TLS build only)
  --tls-cert PATH        PEM/DER certificate chain, leaf certificate first
  --tls-key PATH         Matching unencrypted PEM/DER private key
  --tls-self-signed      HTTPS with a temporary RSA certificate; changes each run
  --tls-handshakes N     Simultaneous TLS handshake ceiling (default: 32)
  --tls-handshake-timeout SEC  Absolute handshake timeout (default: 10)
  --p2p / --no-p2p        Optional browser cooperative downloads (default: off)
  --p2p-peers N          WebRTC connections per page (1-8, default: 4)
  --p2p-cache-mb N       Total verified cache per page (0-64 MiB, default: 64)
  --p2p-upload-kbps N    Shared peer upload KiB/s per page (default: 4096; 0=unlimited)
  --p2p-cache-seconds N  Idle cache expiry, reset on confirmed activity (1-86400s, default: 3600)
  --p2p-seed-seconds N   Compatibility alias for --p2p-cache-seconds
  --mode MODE             filetrans (default) or server (static web hosting)
  --password TEXT         Require a login passphrase (host can change it live)
  --strict-host           Only 127.0.0.1 / ::1 is the host (CLI only)
  --hotlink-seconds SEC   Download session idle timeout; 0 disables (default)
  --show-cli-qr           Print recommended and alternate URL QR codes
  --workspace-app ZIP     Server ZIP application, server.qjs entry, public/ frontend
  --workspace-control-port N  Loopback-only manager/MCP port (0=auto, default)
  --workspace-arg TEXT    Application argument (repeatable); --workspace-args-clear resets
  --workspace-access lan|host  Application clients (default: lan, login still applies)
  --workspace-allow-ip CIDR    Optional IP/CIDR allowlist (repeatable)
  --workspace-public PATH     ZIP static directory (default: public; / for legacy root)
  --workspace-share       Also expose file manager/data APIs to app clients (default: off)
  --script-fs MODE        off, readonly (default), readwrite; confined to --root
  --wasm / --no-wasm      Embedded WAMR interpreter for QuickJS (default: on)
  --wasm-memory-mb N      Total WAMR allocation budget per JS context (64 MiB)
  --wasm-instances N      Live Wasm instances per JS context (8)
  --wasm-instructions N   Interpreter dispatch limit per call/start (1000000)
  --no-compression        Disable HTTP gzip negotiation (enabled by default)
  --compression           Enable HTTP gzip negotiation explicitly
  --open                  Open the local URL in the default browser
  --write-mode MODE       readonly (default), isolated (per-IP), shared
  --attention-click MODE  Attention pills: locate (default) or download
  --no-preview            Disable previews (enabled by default)
  --zip-preview MODE      ZIP preview: off (default), host, client
  --allow-preview         Enable previews explicitly
  --lazy-thumbnails       Enable client-memory thumbnails captured after preview
  --no-lazy-thumbnails    Disable preview thumbnails (default)
  --mcp                   Enable localhost-only MCP (Bearer token printed at startup)
  --no-magika             Disable the embedded Magika content model
  --magika-threads N      Concurrent CPU inferences (default: 2, maximum: 4)
  --cpu-kernel NAME       auto (benchmark), scalar, sse2, avx, avx2-fma,
                          avx512-fma, neon, sve, rvv (available ISA/OS only)
  --ip-limit N            Concurrent distinct client IP ceiling (256; 0=unlimited)
  --owner-limit N         Isolated IP-directory ceiling (256; 0=unlimited)
  --max-upload-mb N       Maximum size per streaming upload (default: 16384)
  --ip-upload-mb N        Per-IP durable cumulative upload MiB (0=unlimited)
  --ip-upload-gb N        Same per-IP allowance expressed in GiB
  --ip-upload-limit IP=N  Override one IP, e.g. 192.168.1.8=5GB (0=unlimited)
  --threads auto|fixed    Adaptive growth/retirement (default: auto)
  --io-threads N          Fixed network worker count
  --file-threads N        Fixed storage worker count
  --metadata-threads N    Fixed metadata worker count
  --io-max N              Adaptive network upper bound (CPU-based)
  --file-max N            Adaptive storage upper bound (CPU-based)
  --metadata-max N        Adaptive metadata upper bound (CPU-based)
  --thread-idle SECONDS   Idle worker retirement delay (default: 15)
  --max-connections N     Connection ceiling (default: 4096)
  --max-transfers N       Active request/transfer ceiling (default: 128)
  --cache-mb N            Maximum adaptive content cache MiB (default: 128; 0=off)
  --buffer-mb N           Additional in-flight buffer budget MiB (default: 64)
  --block-kb N            Large-file block size KiB (default: 256)
  --cache-idle SECONDS    Reclaim unused content after this idle time (default: 60)
  --heat-half-life SEC    Frequency decay half-life (default: 30)
  --no-index              Disable recursive filename search index
  --index-interval SEC    Index rebuild interval (default: 60)
  --index-limit N         Indexed entry ceiling (default: 250000)
  --directory-limit N     Entry ceiling for one directory (default: 100000)
  --timeout SECONDS       Per-operation network timeout (default: 30)
  --help, -h              Show this help
  --version               Show version
  --licenses              Print embedded third-party notices

Examples:
  HttpLocalServer
  HttpLocalServer --root D:\Share --port 9000 --open
  HttpLocalServer server --root ./website --bind 127.0.0.1
)";
}
}
