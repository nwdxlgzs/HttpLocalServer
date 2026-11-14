#include "index.hpp"
#include "config_store.hpp"
#include "platform.hpp"
#include "util.hpp"
#include "file_type.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace hls {
namespace {
void sql(sqlite3* db, const char* statement) {
    if (sqlite3_exec(db, statement, nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
}
class Statement {
public:
    Statement(sqlite3* db, const std::string& text) : db_(db) { if (sqlite3_prepare_v2(db,text.c_str(),-1,&s_,nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db)); }
    ~Statement() { sqlite3_finalize(s_); }
    void text(int index, const std::string& value) { if (sqlite3_bind_text(s_,index,value.c_str(),static_cast<int>(value.size()),SQLITE_TRANSIENT) != SQLITE_OK) throw std::bad_alloc(); }
    void integer(int index, std::int64_t value) { sqlite3_bind_int64(s_,index,value); }
    bool row() { const auto code=sqlite3_step(s_); if (code==SQLITE_ROW) return true; if(code==SQLITE_DONE) return false; throw std::runtime_error(sqlite3_errmsg(db_)); }
    void reset() { sqlite3_reset(s_); sqlite3_clear_bindings(s_); }
    std::string text(int index) { const auto p=sqlite3_column_text(s_,index); return p ? reinterpret_cast<const char*>(p) : ""; }
    std::int64_t integer(int index) { return sqlite3_column_int64(s_,index); }
private: sqlite3* db_; sqlite3_stmt* s_=nullptr;
};
}
std::int64_t file_modified(const std::filesystem::directory_entry& entry) {
    std::error_code ec; const auto time = entry.last_write_time(ec); if (ec) return 0;
    const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(time - decltype(time)::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(system);
}
std::string entry_json(const DirectoryEntry& entry) {
    return "{\"path\":"+json_quote(entry.path)+",\"name\":"+json_quote(entry.name)+",\"directory\":"+(entry.directory?"true":"false")+",\"hidden\":"+(entry.hidden?"true":"false")+",\"size\":"+std::to_string(entry.size)+",\"modified\":"+std::to_string(entry.modified)+",\"type\":"+type_json(extension_type(entry.name),entry.name)+"}";
}
SearchIndex::Snapshot::~Snapshot() { if (db) sqlite3_close(db); }
SearchIndex::SearchIndex(const Config& config,const ConfigStore& protected_files) : config_(config),protected_files_(protected_files) {
}
SearchIndex::~SearchIndex() { stop(); }
void SearchIndex::start() {
    if (!enabled() || thread_.joinable()) return;
    thread_ = std::thread([this] {
        while (!stopping_.load()) {
            scanning_.store(true);
            try { scan(); failed_.store(false); }
            catch (const std::exception& e) { failed_.store(true); std::cerr << "[index] " << e.what() << '\n'; }
            scanning_.store(false);
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock,std::chrono::seconds(config_.index_interval),[this]{return stopping_.load()||dirty_;}); dirty_=false;
        }
    });
}
void SearchIndex::stop() { stopping_.store(true); wake_.notify_all(); if(thread_.joinable()) thread_.join(); }
void SearchIndex::refresh() { { std::lock_guard<std::mutex> lock(mutex_); dirty_=true; } wake_.notify_one(); }
void SearchIndex::scan() {
    auto next=std::make_shared<Snapshot>();
    if(sqlite3_open_v2(":memory:",&next->db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK) throw std::runtime_error("Cannot create search index");
    sql(next->db,"PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA cache_size=-2048; CREATE TABLE entries(path TEXT PRIMARY KEY,name TEXT,dir INTEGER,size INTEGER,modified INTEGER,hidden INTEGER,hidden_depth INTEGER); CREATE VIRTUAL TABLE names USING fts5(path UNINDEXED,name,tokenize='trigram'); BEGIN;");
    Statement insert(next->db,"INSERT INTO entries VALUES(?1,?2,?3,?4,?5,?6,?7)");
    Statement terms(next->db,"INSERT INTO names VALUES(?1,?2)");
    std::error_code ec; std::size_t count=0,metadata_bytes=0; bool truncated=false;
    std::vector<std::size_t> hidden_depths;
    std::filesystem::recursive_directory_iterator iterator(config_.root,std::filesystem::directory_options::skip_permission_denied,ec), end;
    while(iterator!=end && !stopping_.load()) {
        const auto entry=*iterator;
        const auto depth=static_cast<std::size_t>(iterator.depth());
        hidden_depths.resize(depth+1);
        const bool frozen=protected_files_.frozen(entry.path());
        const bool hidden=frozen||entry_hidden(entry.path());
        const auto hidden_depth=hidden?depth+1:depth?hidden_depths[depth-1]:0;
        hidden_depths[depth]=hidden_depth;
        std::error_code status_error; const auto state=entry.symlink_status(status_error);
        bool accept=iterator.depth()<128 && !status_error && !std::filesystem::is_symlink(state) && !link_like(entry.path()) && (std::filesystem::is_regular_file(state)||std::filesystem::is_directory(state));
        if(accept) {
            // Reject junctions escaping the root as well as explicit symbolic links.
            const auto resolved=std::filesystem::canonical(entry.path(),status_error);
            if(status_error || !path_within(config_.root,resolved)) accept=false;
        }
        if(!accept && entry.is_directory(status_error)) iterator.disable_recursion_pending();
        if(accept) {
            auto name=entry.path().filename().u8string();
            if(valid_utf8(name)) {
                auto path="/"+entry.path().lexically_relative(config_.root).generic_u8string();
                const bool placeholder=frozen;
                if(frozen&&std::filesystem::is_directory(state))iterator.disable_recursion_pending();
                try { if(!placeholder)path=normalize_path(path); } catch (...) { accept=false; if(std::filesystem::is_directory(state))iterator.disable_recursion_pending(); }
                if(accept) {
                    if(count>=config_.index_limit || metadata_bytes+path.size()*8+name.size()*16+256>(64ull<<20)) { truncated=true; break; }
                    metadata_bytes+=path.size()*8+name.size()*16+256;
                    const bool directory=std::filesystem::is_directory(state);
                    const auto bytes=directory||frozen ? 0 : entry.file_size(status_error);
                    insert.text(1,path); insert.text(2,name); insert.integer(3,directory?1:0); insert.integer(4,status_error?0:static_cast<std::int64_t>(bytes)); insert.integer(5,frozen?0:file_modified(entry)); insert.integer(6,hidden?1:0); insert.integer(7,static_cast<std::int64_t>(hidden_depth)); insert.row(); insert.reset();
                    terms.text(1,path); terms.text(2,name); terms.row(); terms.reset(); ++count;
                }
            }
        }
        iterator.increment(ec);
        if(ec) ec.clear();
    }
    if(stopping_.load()) return;
    sql(next->db,"COMMIT;");
    { std::lock_guard<std::mutex> lock(mutex_); snapshot_=std::move(next); }
    count_.store(count); truncated_.store(truncated);
}
SearchResult SearchIndex::search(const std::string& query, const std::string& directory, std::size_t offset, std::size_t limit, bool show_hidden) {
    SearchResult result; result.indexing=scanning(); result.truncated=truncated();
    if(!enabled()) throw Failure(503,"Search indexing is disabled");
    if(query.empty() || query.size()>256) throw Failure(400,"Search query must contain 1..256 UTF-8 bytes");
    std::shared_ptr<Snapshot> snapshot;
    { std::lock_guard<std::mutex> lock(mutex_); snapshot=snapshot_; }
    if(!snapshot) { result.indexing=!failed(); return result; }
    std::size_t characters=0; for(unsigned char c:query) if((c&0xc0)!=0x80) ++characters;
    const bool trigram=characters>=3;
    std::string expression="\""; for(char c:query) { expression+=c; if(c=='"') expression+='"'; } expression+='"';
    const std::string from=trigram ? " FROM names JOIN entries e ON e.path=names.path WHERE names MATCH ?1" : " FROM entries e WHERE instr(lower(e.name),lower(?1))>0";
    const std::string scope=" AND substr(e.path,1,length(?2))=?2"+std::string(show_hidden?"":" AND e.hidden_depth<=?5");
    const auto prefix=directory=="/" ? "/" : directory+"/";
    const auto scope_depth=directory=="/"?0:std::count(directory.begin(),directory.end(),'/');
    std::lock_guard<std::mutex> lock(snapshot->mutex);
    Statement total(snapshot->db,"SELECT count(*)"+from+scope);
    total.text(1,trigram?expression:query); total.text(2,prefix); if(!show_hidden)total.integer(5,scope_depth); if(total.row()) result.total=static_cast<std::size_t>(total.integer(0));
    Statement rows(snapshot->db,"SELECT e.path,e.name,e.dir,e.size,e.modified,e.hidden"+from+scope+" ORDER BY e.dir DESC,e.name COLLATE NOCASE,e.path LIMIT ?3 OFFSET ?4");
    rows.text(1,trigram?expression:query); rows.text(2,prefix); rows.integer(3,static_cast<std::int64_t>(limit)); rows.integer(4,static_cast<std::int64_t>(offset));
    if(!show_hidden)rows.integer(5,scope_depth);
    while(rows.row()) result.entries.push_back({rows.text(0),rows.text(1),rows.integer(2)!=0,static_cast<std::uint64_t>(rows.integer(3)),rows.integer(4),rows.integer(5)!=0});
    return result;
}
}
