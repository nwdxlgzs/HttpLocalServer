#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <map>

namespace hls {
struct MountConfig {std::string path;std::filesystem::path source;bool writable=false;};
struct Config {
    std::filesystem::path config_path;
    bool config_explicit=false;
    std::filesystem::path state_dir,script_path,workspace_app;
    unsigned script_memory_mb=16,script_timeout_ms=50,script_concurrency=4;
    std::vector<std::string> workspace_args,workspace_allow_ips;
    std::string workspace_access="lan",script_fs="readonly",workspace_public="public";
    bool workspace_share=false,wasm=true;
    unsigned wasm_memory_mb=64,wasm_instances=8,wasm_instructions=1000000;
    std::uint16_t workspace_control_port=0;
    std::size_t workspace_cache_bytes=16ull<<20;
    std::string default_documents="index.html,index.htm",spa_fallback;
    std::map<std::string,std::string> response_headers,error_pages;
    bool recycle=false;
    unsigned recycle_days=7,resume_hours=24;
    std::uint64_t recycle_bytes=1024ull<<20,resume_bytes=64ull<<30;
    unsigned ip_transfers=0;
    std::uint64_t upload_kbps=0,download_kbps=0,ip_upload_kbps=0,ip_download_kbps=0;
    std::vector<std::string> protected_file_ids;
    std::string title="在线文件下载",announcement;
    std::vector<std::string> attention_paths;
    std::string mode = "filetrans", bind = "0.0.0.0";
    std::string write_mode = "readonly";
    std::string attention_click = "locate";
    std::filesystem::path root = std::filesystem::current_path();
    std::uint16_t port = 8000;
    unsigned io_threads = 4, file_threads = 4, metadata_threads = 2;
    unsigned io_max = 8, file_max = 8, metadata_max = 4, thread_idle = 15;
    bool adaptive_threads = true, fixed_io = false, fixed_file = false, fixed_metadata = false;
    std::size_t max_connections = 4096, max_transfers = 128;
    std::size_t ip_limit = 256, owner_limit = 256;
    std::uint64_t max_upload_bytes = 16ull << 30;
    std::uint64_t ip_upload_bytes=0;
    std::map<std::string,std::uint64_t> ip_upload_overrides;
    std::size_t cache_bytes = 128ull << 20, buffer_bytes = 64ull << 20;
    std::size_t block_bytes = 256ull << 10, index_limit = 250000, directory_limit = 100000;
    unsigned cache_idle = 60, heat_half_life = 30, index_interval = 60, timeout = 30;
    bool open_browser = false, index = true, help = false, licenses = false, version = false;
    bool allow_preview = true,thumbnails=false;
    std::string zip_preview="off";
    bool mcp=false;
    bool magika=true;
    std::string cpu_kernel="auto";
    unsigned magika_threads=2;
    bool system_root=false;
    bool strict_host=false,show_cli_qr=false,compression=true;
    bool https=false,tls_self_signed=false;
    bool p2p=false;
    unsigned p2p_peers=4,p2p_cache_mb=64,p2p_upload_kbps=4096,p2p_seed_seconds=3600;
    std::filesystem::path tls_cert,tls_key;
    unsigned tls_handshakes=32,tls_handshake_timeout=10;
    std::string login_password;
    unsigned hotlink_seconds=0;
    std::vector<MountConfig> mounts;
    std::vector<std::string> volume_mounts,root_mounts;
};
Config parse_config(int argc, char** argv);
std::string help_text();
}
