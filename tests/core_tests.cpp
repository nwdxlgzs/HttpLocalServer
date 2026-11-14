#include "cache.hpp"
#include "util.hpp"
#include "sha256.hpp"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void hashes(){
    hls::Sha256 empty;require(empty.finish()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA-256 empty message");
    hls::Sha256 abc;abc.update("a",1);abc.update("bc",2);require(abc.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 incremental known vector");
    hls::Sha256 million;const std::string part(1000,'a');for(unsigned i=0;i<1000;++i)million.update(part.data(),part.size());require(million.finish()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA-256 long known vector");
}
void ranges() {
    auto r=hls::parse_range("bytes=4294967296-4294967300",8589934592ull);
    require(r.state==hls::RangeState::valid && r.first==4294967296ull && r.length==5,"64-bit closed range");
    r=hls::parse_range("bytes=-8",5); require(r.first==0 && r.length==5,"Oversized suffix clamps to size");
    r=hls::parse_range("bytes=7-",10); require(r.first==7 && r.length==3,"Open-ended range");
    require(hls::parse_range("bytes=0-0",0).state==hls::RangeState::unsatisfiable,"Empty file has no satisfiable range");
    require(hls::parse_range("bytes=10-",10).state==hls::RangeState::unsatisfiable,"EOF range rejected");
    require(hls::parse_range("bytes=1-2,4-5",10).state==hls::RangeState::ignored,"Unsupported multirange ignored");
    require(hls::parse_range("bytes=18446744073709551616-",10).state==hls::RangeState::ignored,"Overflow rejected");
    require(hls::parse_range("bytes=-0",10).state==hls::RangeState::unsatisfiable,"Zero suffix rejected");
}
void paths() {
    require(hls::normalize_path("//a/./b/")=="/a/b","Path normalization");
    for(const auto& path:{"/../secret","/a/../../b","/a\\b","/x:stream","/trailing.","/trailing "}) {
        bool denied=false; try { hls::normalize_path(path); } catch(const std::invalid_argument&) {denied=true;} require(denied,"Unsafe path rejected");
    }
    require(hls::url_decode("%E4%B8%AD%E6%96%87+%2B",true)==u8"中文 +","Unicode query decoding");
    bool denied=false; try{hls::url_decode("%c0%af");}catch(...){denied=true;} require(denied,"Overlong UTF-8 rejected");
    require(hls::etag_matches("\"other\", W/\"version\"","W/\"version\""),"Weak conditional comparison");
}
void cache() {
    const auto root=std::filesystem::temp_directory_path()/("hls-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    try {
        { std::ofstream out(root/"data.bin",std::ios::binary); const std::string data(32768,'X'); out.write(data.data(),static_cast<std::streamsize>(data.size())); }
        hls::Config config; config.cache_bytes=16ull<<20; config.buffer_bytes=1ull<<20; config.block_bytes=4096; config.cache_idle=1; config.heat_half_life=1;
        hls::WorkerPool workers(2,64); hls::HotCache cache(config,workers); auto file=hls::File::open(hls::canonical_root(root),"/data.bin");
        std::mutex mutex; std::condition_variable ready; unsigned completed=0; bool correct=true;
        auto callback=[&](std::exception_ptr error,std::shared_ptr<hls::Block> block) {
            std::lock_guard<std::mutex> lock(mutex); correct=correct && !error && block && block->size==4096 && block->data[0]=='X' && block->data[4095]=='X'; ++completed; ready.notify_all();
        };
        for(unsigned i=0;i<32;++i) cache.get(file,0,callback);
        { std::unique_lock<std::mutex> lock(mutex); require(ready.wait_for(lock,std::chrono::seconds(10),[&]{return completed==32;}),"All concurrent cache waiters complete"); }
        require(correct,"Concurrent block reads preserve content");
        cache.get(file,0,callback);
        { std::unique_lock<std::mutex> lock(mutex); require(ready.wait_for(lock,std::chrono::seconds(10),[&]{return completed==33;}),"Cache hit completes"); }
        auto before=cache.stats(); require(before.hits>0 && before.disk_bytes==4096,"Repeated hot block is read once");
        require(before.resident_bytes>0 && before.allocated_bytes>0,"Hot block resides in cache");
        std::this_thread::sleep_for(std::chrono::milliseconds(1200)); cache.maintain();
        auto after=cache.stats(); require(after.resident_bytes==0 && after.allocated_bytes==0 && after.released_bytes>=4096,"Idle cache actually returns OS pages");
        { std::ofstream out(root/"data.bin",std::ios::binary|std::ios::app); out.put('Y'); }
        auto updated=hls::File::open(hls::canonical_root(root),"/data.bin"); require(updated->info().version!=file->info().version,"Mutation changes cache identity");
        workers.stop();
    } catch(...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
}
void scan_resistance() {
    const auto root=hls::canonical_root(std::filesystem::temp_directory_path())/("hls-scan-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    try {
        { std::ofstream out(root/"blocks.bin",std::ios::binary); const std::string block(4096,'S'); for(int i=0;i<512;++i) out.write(block.data(),4096); }
        hls::Config config; config.cache_bytes=1ull<<20; config.buffer_bytes=1ull<<20; config.block_bytes=4096;
        hls::WorkerPool workers(2,64); hls::HotCache cache(config,workers); auto file=hls::File::open(root,"/blocks.bin");
        auto read=[&](std::uint64_t offset) {
            std::mutex mutex; std::condition_variable ready; bool done=false; std::exception_ptr failure;
            cache.get(file,offset,[&](std::exception_ptr error,std::shared_ptr<hls::Block> block) {
                std::lock_guard<std::mutex> lock(mutex); failure=error;
                if(!error && (!block || block->data[0]!='S')) failure=std::make_exception_ptr(std::runtime_error("Block content mismatch"));
                done=true; ready.notify_one();
            });
            std::unique_lock<std::mutex> lock(mutex); require(ready.wait_for(lock,std::chrono::seconds(10),[&]{return done;}),"Scan read completes");
            if(failure) std::rethrow_exception(failure);
        };
        for(int i=0;i<50;++i) read(0);
        for(int i=1;i<512;++i) read(static_cast<std::uint64_t>(i)*4096);
        const auto before=cache.stats(); read(0); const auto after=cache.stats();
        require(after.hits==before.hits+1 && after.disk_bytes==before.disk_bytes,"One-pass cold scan preserves an established hot block");
        require(after.resident_bytes<=config.cache_bytes,"Scan remains within content budget");
        workers.stop();
    } catch(...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
}
}
int main() { try { hashes(); ranges(); paths(); cache(); scan_resistance(); std::cout<<"Core tests passed\n"; return 0; }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
