// SPDX-License-Identifier: Apache-2.0
#include "state_store.hpp"
#include "platform.hpp"
#include "util.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace hls {
namespace {
void safe(const std::filesystem::path &path) {
    auto p = path.root_path();
    for (const auto &part : path.relative_path()) {
        p /= part;
        if (link_like(p))
            throw Failure(403, "State directory cannot contain links");
    }
}
struct Statement {
    sqlite3_stmt *p = nullptr;
    ~Statement() {
        if (p)
            sqlite3_finalize(p);
    }
};
void prepare(sqlite3 *db, Statement &s, const char *sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &s.p, nullptr) != SQLITE_OK)
        throw Failure(500, "Cannot prepare persistent state");
}
void bind(Statement &s, int i, const std::string &v) {
    if (sqlite3_bind_text(s.p, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throw Failure(500, "Cannot bind persistent state");
}
void done(Statement &s) {
    if (sqlite3_step(s.p) != SQLITE_DONE)
        throw Failure(507, "Cannot commit persistent state");
}
} // namespace
StateStore::StateStore(const Config &c) {
    auto base = c.config_path.empty() ? c.root : c.config_path.parent_path();
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : std::filesystem::absolute(c.root).lexically_normal().u8string()) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    root_ = std::filesystem::absolute(c.state_dir.empty() ? base / (".hls-state-" + std::to_string(hash)) : c.state_dir)
                .lexically_normal();
    safe(root_);
    std::filesystem::create_directories(root_);
    safe(root_);
    root_ = canonical_root(root_);
#ifndef _WIN32
    if (chmod(root_.c_str(), 0700) != 0)
        throw Failure(403, "Cannot secure state directory");
#endif
    protect();
#ifdef _WIN32
    auto handle = CreateFileW((root_ / "owner.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw Failure(409, "State directory is already in use");
    lock_ = reinterpret_cast<std::intptr_t>(handle);
#else
    lock_ = ::open((root_ / "owner.lock").c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (lock_ < 0)
        throw Failure(403, "Cannot lock state directory");
    if (flock(static_cast<int>(lock_), LOCK_EX | LOCK_NB) < 0) {
        close(static_cast<int>(lock_));
        lock_ = -1;
        throw Failure(409, "State directory is already in use");
    }
#endif
    const auto file = (root_ / "state.sqlite").u8string();
    if (sqlite3_open_v2(file.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw Failure(500, "Cannot open persistent state");
    }
    sqlite3_busy_timeout(db_, 3000);
    if (sqlite3_exec(db_,
                     "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS kv(scope TEXT NOT "
                     "NULL,k TEXT NOT NULL,v TEXT NOT NULL,PRIMARY KEY(scope,k)) WITHOUT ROWID;",
                     nullptr, nullptr, nullptr) != SQLITE_OK)
        throw Failure(500, "Cannot initialize persistent state");
#ifndef _WIN32
    chmod((root_ / "state.sqlite").c_str(), 0600);
#endif
    for (const auto *name : {"state.sqlite", "state.sqlite-wal", "state.sqlite-shm"}) {
        const auto id = file_identity(root_ / name);
        if (!id.empty())
            identities_.insert(id);
    }
}
StateStore::~StateStore() {
    if (db_) {
        sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
        sqlite3_close(db_);
    }
#ifdef _WIN32
    if (lock_ != -1)
        CloseHandle(reinterpret_cast<HANDLE>(lock_));
#else
    if (lock_ >= 0)
        close(static_cast<int>(lock_));
#endif
}
void StateStore::protect() const {
    safe(root_);
    for (const auto *n : {"state.sqlite", "state.sqlite-wal", "state.sqlite-shm", "owner.lock"})
        if (link_like(root_ / n))
            throw Failure(403, "Persistent state cannot be a link");
}
bool StateStore::protects(const std::filesystem::path &candidate, bool ancestor) const {
    const auto p = std::filesystem::absolute(candidate).lexically_normal();
    if (path_within(root_, p) || (ancestor && path_within(p, root_)))
        return true;
#ifdef __APPLE__
    // APFS/HFS may fold case even though std::filesystem paths compare bytes.
    for (auto at = p; !at.empty(); at = at.parent_path()) {
        std::error_code ec;
        if (std::filesystem::equivalent(at, root_, ec))
            return true;
        if (at == at.root_path())
            break;
    }
    if (ancestor)
        for (auto at = root_; !at.empty(); at = at.parent_path()) {
            std::error_code ec;
            if (std::filesystem::equivalent(at, p, ec))
                return true;
            if (at == at.root_path())
                break;
        }
#endif
    const auto id = file_identity(p);
    return !id.empty() && identities_.count(id) != 0;
}
std::string StateStore::get(const std::string &scope, const std::string &key) {
    std::lock_guard<std::mutex> l(mutex_);
    protect();
    Statement s;
    prepare(db_, s, "SELECT v FROM kv WHERE scope=? AND k=?");
    bind(s, 1, scope);
    bind(s, 2, key);
    const auto rc = sqlite3_step(s.p);
    if (rc == SQLITE_ROW)
        return reinterpret_cast<const char *>(sqlite3_column_text(s.p, 0));
    if (rc != SQLITE_DONE)
        throw Failure(500, "Cannot read persistent state");
    return {};
}
std::map<std::string, std::string> StateStore::scan(const std::string &scope) {
    std::lock_guard<std::mutex> l(mutex_);
    protect();
    Statement s;
    prepare(db_, s, "SELECT k,v FROM kv WHERE scope=? ORDER BY k LIMIT 65536");
    bind(s, 1, scope);
    std::map<std::string, std::string> out;
    int rc;
    while ((rc = sqlite3_step(s.p)) == SQLITE_ROW)
        out.emplace(reinterpret_cast<const char *>(sqlite3_column_text(s.p, 0)),
                    reinterpret_cast<const char *>(sqlite3_column_text(s.p, 1)));
    if (rc != SQLITE_DONE)
        throw Failure(500, "Cannot read persistent state");
    return out;
}
void StateStore::put(const std::string &scope, const std::string &key, const std::string &value) {
    if (key.size() > 4096 || value.size() > 65536)
        throw Failure(413, "Persistent state entry too large");
    std::lock_guard<std::mutex> l(mutex_);
    protect();
    Statement s;
    prepare(db_, s, "INSERT INTO kv(scope,k,v) VALUES(?,?,?) ON CONFLICT(scope,k) DO UPDATE SET v=excluded.v");
    bind(s, 1, scope);
    bind(s, 2, key);
    bind(s, 3, value);
    done(s);
}
void StateStore::erase(const std::string &scope, const std::string &key) {
    std::lock_guard<std::mutex> l(mutex_);
    Statement s;
    prepare(db_, s, "DELETE FROM kv WHERE scope=? AND k=?");
    bind(s, 1, scope);
    bind(s, 2, key);
    done(s);
}
void StateStore::transfer_checkpoint(const std::string& key,const std::string& metadata,const std::string& journal){
    if(key.size()>128||metadata.size()>65536||journal.size()>4u*1024u*1024u)throw Failure(413,"Transfer checkpoint exceeds bound");
    std::lock_guard<std::mutex> lock(mutex_);protect();
    if(sqlite3_exec(db_,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK)throw Failure(507,"Cannot begin checkpoint transaction");
    try{
        for(const auto& row:std::vector<std::pair<std::string,std::string>>{{"transfer-cache",metadata},{"transfer-journal",journal}}){Statement s;prepare(db_,s,"INSERT INTO kv(scope,k,v) VALUES(?,?,?) ON CONFLICT(scope,k) DO UPDATE SET v=excluded.v");bind(s,1,row.first);bind(s,2,key);bind(s,3,row.second);done(s);}
        if(sqlite3_exec(db_,"COMMIT",nullptr,nullptr,nullptr)!=SQLITE_OK)throw Failure(507,"Cannot commit checkpoint transaction");
    }catch(...){sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
} // namespace hls
