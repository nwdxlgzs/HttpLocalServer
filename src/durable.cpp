// SPDX-License-Identifier: Apache-2.0
#include "service.hpp"
#include "util.hpp"
#include "sha256.hpp"
#include <boost/json.hpp>
#include <boost/asio/ip/address.hpp>
#include <algorithm>
#include <ctime>
namespace hls {
namespace {
namespace json = boost::json;
json::object object(const std::string &bytes) {
    boost::system::error_code ec;
    json::parse_options p;
    p.max_depth = 12;
    auto v = json::parse(bytes, ec, {}, p);
    if (ec || !v.is_object())
        throw Failure(400, "Expected JSON object");
    return v.as_object();
}
std::string str(const json::object &o, const char *key, const std::string &fallback = "") {
    const auto *v = o.if_contains(key);
    if (!v)
        return fallback;
    if (!v->is_string() || v->as_string().size() > 8192)
        throw Failure(400, std::string("Invalid ") + key);
    return std::string(v->as_string());
}
std::uint64_t num(const json::object &o, const char *key, std::uint64_t fallback = 0) {
    const auto *v = o.if_contains(key);
    if (!v)
        return fallback;
    if (!v->is_uint64() && !v->is_int64())
        throw Failure(400, "Expected unsigned integer");
    if (v->is_int64() && v->as_int64() < 0)
        throw Failure(400, "Negative integer");
    return v->is_uint64() ? v->as_uint64() : static_cast<std::uint64_t>(v->as_int64());
}
std::string param(const Request &r, const char *k) {
    const auto p = query_params(r.target);
    const auto i = p.find(k);
    return i == p.end() ? "" : i->second;
}
Response encoded(json::object o, unsigned status = 200) {
    Response r;
    r.status = status;
    r.body = json::serialize(o);
    r.headers = {{"Cache-Control", "no-store"}};
    return r;
}
std::uint64_t now() { return static_cast<std::uint64_t>(std::time(nullptr)); }
bool id_ok(const std::string &id) {
    return id.size() == 48 &&
           std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool within(const std::string &a, const std::string &b) {
    return a == "/" || a == b || (b.size() > a.size() && b.compare(0, a.size(), a) == 0 && b[a.size()] == '/');
}
void remove_spool(StateStore &state, const std::string &id) {
    try {
        secure_remove(state.root(), "/resume-" + id, false, 1);
    } catch (const Failure &e) {
        if (e.status != 404)
            throw;
    }
}
std::filesystem::path trash_root(const std::filesystem::path &root) {
    auto p = root / ".hls-trash";
    if (link_like(p))
        throw Failure(403, "Unsafe recycle store");
    std::filesystem::create_directory(p);
    if (link_like(p))
        throw Failure(403, "Unsafe recycle store");
    return p;
}
std::uint64_t tree_size(const std::filesystem::path &root, const std::string &path, std::size_t limit) {
    const auto p = resolve_path(root, path);
    std::error_code ec;
    const auto st = std::filesystem::symlink_status(p, ec);
    if (ec)
        throw Failure(404, "Entry disappeared");
    if (link_like(p))
        throw Failure(403, "Cannot recycle a link");
    if (std::filesystem::is_regular_file(st))
        return std::filesystem::file_size(p);
    std::uint64_t size = 0;
    unsigned count = 0;
    for (std::filesystem::recursive_directory_iterator it(p), end; it != end; ++it) {
        if (++count > limit || it.depth() > 64)
            throw Failure(413, "Recycle traversal limit exceeded");
        if (link_like(it->path()))
            throw Failure(403, "Cannot recycle trees containing links");
        if (it->is_regular_file()) {
            const auto n = it->file_size();
            if (n > UINT64_MAX - size)
                throw Failure(413, "Recycle size overflow");
            size += n;
        }
    }
    return size;
}
} // namespace
void Service::persist_alias(const std::string &path, const Alias *a) {
    if (!a) {
        state_.erase("alias", path);
        return;
    }
    state_.put(
        "alias", path,
        json::serialize(json::object{
            {"approved", a->approved}, {"pending", a->pending}, {"revision", a->revision}, {"uploaded", a->uploaded}}));
}
void Service::persist_login(const std::string &ip, const LoginAttempt &attempt) {
    state_.put("login", ip,
               json::serialize(json::object{{"failures", attempt.failures},
                                            {"round", attempt.round},
                                            {"until", now() + attempt.wait(LoginAttempt::Clock::now())},
                                            {"used", now()}}));
}
void Service::complete_resume(const std::string &id, std::uint64_t expires) {
    std::lock_guard<std::mutex> lock(resume_mutex_);
    const auto found = resume_summary_.find(id);
    if (found != resume_summary_.end()) {
        found->second.done = true;
        found->second.expires = expires;
    }
}
void Service::restore_durable() {
    if(config_.recycle)config_store_.depend(config_.root/".hls-trash",true);
    for(const auto& entry:state_.scan("trash")){auto data=object(entry.second);config_store_.depend(std::filesystem::u8path(str(data,"root"))/".hls-trash",true);}
    for (const auto &e : state_.scan("resume")) {
        const auto v = object(e.second);
        resume_summary_[e.first] = {str(v, "ip"), num(v, "size"), num(v, "expires"), str(v, "phase") == "done"};
    }

    for (const auto &e : state_.scan("login")) {
        const auto v = object(e.second);
        const auto until = num(v, "until"), used = num(v, "used");
        if (used + 48ull * 3600 < now() && until < now()) {
            state_.erase("login", e.first);
            continue;
        }
        LoginAttempt a;
        a.failures = static_cast<unsigned>(std::min<std::uint64_t>(4, num(v, "failures")));
        a.round = static_cast<unsigned>(std::min<std::uint64_t>(11, num(v, "round")));
        a.used = LoginAttempt::Clock::now() -
                 std::chrono::seconds(std::min<std::uint64_t>(48ull * 3600, now() > used ? now() - used : 0));
        a.until = LoginAttempt::Clock::now() +
                  std::chrono::seconds(std::min<std::uint64_t>(86400, until > now() ? until - now() : 0));
        login_attempts_[e.first] = a;
    }

    for (const auto &v : state_.scan("quota"))
        quota_->used[v.first] = parse_unsigned(v.second, UINT64_MAX);
    for (const auto &v : state_.scan("alias")) {
        if (aliases_.size() >= 4096)
            break;
        const auto o = object(v.second);
        Alias a;
        a.approved = str(o, "approved");
        a.pending = str(o, "pending");
        a.revision = num(o, "revision");
        const auto *uploaded = o.if_contains("uploaded");
        a.uploaded = uploaded && uploaded->is_bool() && uploaded->as_bool();
        aliases_[v.first] = a;
    }
    ++alias_revision_;
    maintain_durable();
}
std::shared_ptr<Service::ResumeLock> Service::resume_lock(const std::string &id) {
    std::lock_guard<std::mutex> l(resume_mutex_);
    for (auto it = resume_locks_.begin(); it != resume_locks_.end();)
        if (it->second.expired())
            it = resume_locks_.erase(it);
        else
            ++it;
    auto &weak = resume_locks_[id];
    auto strong = weak.lock();
    if (!strong) {
        strong = std::make_shared<ResumeLock>();
        weak = strong;
    }
    return strong;
}
Response Service::resume_api(const Request &req, const std::string &route) {
    browser_guard(req, req.method != "GET");
    if (route == "/api/resume/copy-chunk" && req.method == "POST") {
        const auto input = object(req.body);
        const auto source = normalize_path(str(input,"source"));
        const auto expected = str(input,"version");
        if (expected.empty() || expected.size() > 512)
            throw Failure(400,"Expected pinned copy source version");
        protect_config(source);
        script_file(req,"read",source);
        auto file = open_file(source);
        if (file->info().version != expected)
            throw Failure(409,"Copy source changed");
        const auto at = num(input,"offset");
        if (at >= file->info().size)
            throw Failure(400,"Copy offset exceeds source");
        auto chunk = req;
        chunk.target = "/api/resume/chunk?id=" + url_encode(str(input,"id")) + "&key=" +
                       url_encode(str(input,"key")) + "&offset=" + std::to_string(at);
        chunk.body.resize(static_cast<std::size_t>(std::min<std::uint64_t>(1048576,file->info().size-at)));
        if (file->read(at,chunk.body.data(),chunk.body.size()) != chunk.body.size() || !file->unchanged())
            throw Failure(409,"Copy source changed during read");
        return resume_api(chunk,"/api/resume/chunk");
    }
    if (route == "/api/resume/create") {
        if (req.method != "POST")
            throw Failure(405, "Use POST");
        const auto o = object(req.body);
        const auto size = num(o, "size");
        if (size > settings()->max_upload_bytes)
            throw Failure(413, "Upload exceeds maximum");
        const auto fingerprint = str(o, "fingerprint");
        if (fingerprint.empty() || fingerprint.size() > 256)
            throw Failure(400, "Expected bounded file fingerprint");
        auto checking = req;
        checking.target = "/api/upload?path=" + url_encode(normalize_path(str(o, "path"))) +
                          "&parents=1&drop=" + url_encode(str(o, "drop")) +
                          "&replaceVersion=" + url_encode(str(o, "replaceVersion")) +
                          "&mirror=" + url_encode(str(o, "mirror")) +
                          "&sourceVersion=" + url_encode(str(o, "sourceVersion"));
        checking.headers["content-length"] = std::to_string(size);
        // Reuse the ordinary upload admission path, including mount proofs,
        // frozen identities, version checks and IP ownership. No publication.
        auto admission = begin_upload(checking);
        const auto path = admission->path();
        admission.reset();
        std::lock_guard<std::mutex> caches(transfer_cache_mutex_);
        std::lock_guard<std::mutex> l(resume_mutex_);
        // Keep active reservations bounded. Completed receipts are a bounded
        // idempotency window, rather than a lifetime upload-count limit.
        while (resume_summary_.size() >= 1024) {
            auto oldest = resume_summary_.end();
            for (auto it = resume_summary_.begin(); it != resume_summary_.end(); ++it)
                if (it->second.done && (oldest == resume_summary_.end() || it->second.expires < oldest->second.expires))
                    oldest = it;
            if (oldest == resume_summary_.end())
                break;
            remove_spool(state_, oldest->first);
            state_.erase("resume", oldest->first);
            resume_summary_.erase(oldest);
        }
        std::uint64_t reserved = 0;
        for(const auto& cache:state_.scan("transfer-cache")){const auto bytes=num(object(cache.second),"offset");if(bytes>config_.resume_bytes||reserved>config_.resume_bytes-bytes)throw Failure(507,"Transfer storage budget reached");reserved+=bytes;}
        unsigned own = 0;
        for (const auto &e : resume_summary_) {
            const auto &v = e.second;
            if (!v.done) {
                if (v.size > UINT64_MAX - reserved)
                    throw Failure(507, "Upload reserve overflow");
                reserved += v.size;
                if (v.ip == req.peer_ip)
                    ++own;
            }
        }
        if (resume_summary_.size() >= 1024 || own >= 16 || size > config_.resume_bytes ||
            reserved > config_.resume_bytes - size)
            throw Failure(507, "Resumable upload storage budget reached");
        const auto id = random_token(), key = random_token();
        json::object v{{"ip", req.peer_ip},
                       {"key", key},
                       {"path", path},
                       {"target", checking.target},
                       {"size", size},
                       {"offset", 0},
                       {"fingerprint", fingerprint},
                       {"expires", now() + config_.resume_hours * 3600ull},
                       {"chain", ""},
                       {"phase", "receiving"},
                       {"root", mapped(path).root.u8string()},
                       {"storage", mapped(path).path}};
        state_.put("resume", id, json::serialize(v));
        try {
            secure_spool(state_.root(), "/resume-" + id, 0, "");
        } catch (...) {
            state_.erase("resume", id);
            throw;
        }
        resume_summary_[id] = {req.peer_ip, size, num(v, "expires"), false};
        return encoded({{"id", id}, {"key", key}, {"offset", 0}, {"path", path}, {"chunkBytes", 1048576}}, 201);
    }
    const auto id = param(req, "id"), key = param(req, "key");
    if (!id_ok(id) || !id_ok(key))
        throw Failure(400, "Invalid upload capability");
    auto guard = resume_lock(id);
    bool available = false;
    if (!guard->busy.compare_exchange_strong(available, true))
        throw Failure(409, "This upload already has an active operation");
    auto activity = std::shared_ptr<void>(reinterpret_cast<void *>(1), [guard](void *) { guard->busy = false; });
    const auto bytes = state_.get("resume", id);
    if (bytes.empty())
        throw Failure(404, "Upload expired or not found");
    auto v = object(bytes);
    if (str(v, "key") != key || str(v, "ip") != req.peer_ip)
        throw Failure(403, "Upload belongs to a different client");
    if (num(v, "expires") < now())
        throw Failure(410, "Upload expired");
    const auto path = str(v, "path"), phase = str(v, "phase");
    const auto size = num(v, "size"), offset = num(v, "offset");
    if (route == "/api/resume/status" && req.method == "GET")
        return encoded({{"id", id},
                        {"path", path},
                        {"size", size},
                        {"offset", offset},
                        {"fingerprint", str(v, "fingerprint")},
                        {"chain", str(v, "chain")},
                        {"phase", phase},
                        {"expires", num(v, "expires")}});
    if (req.method != "POST")
        throw Failure(405, "Use POST");
    if (route == "/api/resume/cancel") {
        const auto temporary = str(v, "temporary");
        if (!temporary.empty()) {
            try {
                discard_upload_temporary(std::filesystem::u8path(str(v, "root")), str(v, "storage", path), temporary);
            } catch (const Failure &e) {
                if (e.status != 404)
                    throw;
            }
        }
        remove_spool(state_, id);
        state_.erase("resume", id);
        {
            std::lock_guard<std::mutex> summary(resume_mutex_);
            resume_summary_.erase(id);
        }
        return encoded({{"ok", true}});
    }
    if (phase == "done") {
        if (route == "/api/resume/commit") {
            Response r;
            r.status = 200;
            r.body = str(v, "result");
            return r;
        }
        throw Failure(409, "Upload already committed");
    }
    if (mapped(path).root.u8string() != str(v, "root"))
        throw Failure(409, "Destination mount changed");
    auto effective = req;
    effective.target = str(v, "target");
    effective.headers["content-length"] = std::to_string(size);
    effective.prepaid = true;
    if (size > settings()->max_upload_bytes)
        throw Failure(413, "Host reduced the per-file upload limit");
    if (route == "/api/resume/rebind") {
        const auto body = object(req.body);
        const auto target = str(body, "target");
        if (target.rfind("/api/upload?", 0) != 0 || target.size() > 8192)
            throw Failure(400, "Invalid rebound upload target");
        auto checking = req;
        checking.target = target;
        checking.prepaid = true;
        checking.headers["content-length"] = std::to_string(size);
        auto admission = begin_upload(checking);
        if (admission->path() != path || mapped(path).root.u8string() != str(v, "root"))
            throw Failure(403, "Rebinding cannot change the upload destination");
        admission.reset();
        v["target"] = target;
        state_.put("resume", id, json::serialize(v));
        return encoded({{"ok", true}});
    }
    if (route == "/api/resume/chunk") {
        const auto at = parse_unsigned(param(req, "offset"), UINT64_MAX);
        if (req.body.empty() || req.body.size() > 1048576 || at > size || req.body.size() > size - at)
            throw Failure(400, "Invalid upload chunk bounds");
        if (at < offset) {
            if (req.body.size() > offset - at)
                throw Failure(409, "Chunk overlaps acknowledged offset");
            auto f = File::open(state_.root(), "/resume-" + id);
            std::string old(req.body.size(), '\0');
            if (f->read(at, old.data(), old.size()) != old.size() || old != req.body)
                throw Failure(409, "Retry chunk content differs");
            return encoded({{"offset", offset}});
        }
        if (at != offset)
            throw Failure(409, "Unexpected upload offset");
        if (at % 1048576 || req.body.size() != std::min<std::uint64_t>(1048576, size - at))
            throw Failure(400, "Chunks must follow the negotiated 1 MiB boundaries");
        {
            std::lock_guard<std::mutex> w(write_mutex_);
            const auto mirror = param(effective, "mirror");
            Mount mount;
            if (mirror.empty())
                authorize(req, path);
            else if (!remote_mount(path, &mount) || mount.key != mirror || mount.peer != req.peer_ip)
                throw Failure(403, "Invalid mount replica provider");
            protect_config(path);
        }
        if (settings()->write_mode != "isolated" || !req.local_admin)
            charge_upload(req.peer_ip, req.body.size());
        secure_spool(state_.root(), "/resume-" + id, offset, req.body);
        Sha256 piece;
        piece.update(req.body.data(), req.body.size());
        const auto material = str(v, "chain") + piece.finish();
        Sha256 chain;
        chain.update(material.data(), material.size());
        v["chain"] = chain.finish();
        v["offset"] = offset + req.body.size();
        v["expires"] = now() + config_.resume_hours * 3600ull;
        state_.put("resume", id, json::serialize(v));
        return encoded({{"offset", offset + req.body.size()}});
    }
    if (route != "/api/resume/commit")
        throw Failure(404, "Unknown upload operation");
    if (offset != size)
        throw Failure(409, "Upload is incomplete");
    // Publication is recoverable if the process stops after COW rename and
    // before recording the result. Compare the durable spool once on retry.
    if (phase == "committing") {
        try {
            const auto dest = open_file(path), spool = File::open(state_.root(), "/resume-" + id);
            bool same = dest->info().size == size;
            std::vector<char> a(262144), b(a.size());
            for (std::uint64_t at = 0; same && at < size;) {
                const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(a.size(), size - at));
                same = dest->read(at, a.data(), n) == n && spool->read(at, b.data(), n) == n &&
                       std::equal(a.begin(), a.begin() + n, b.begin());
                at += n;
            }
            if (same) {
                v["phase"] = "done";
                v["expires"] = now() + 600;
                v["result"] =
                    json::serialize(json::object{{"path", path}, {"size", size}, {"version", dest->info().version}});
                state_.put("resume", id, json::serialize(v));
                complete_resume(id, num(v, "expires"));
                remove_spool(state_, id);
                Response r;
                r.body = str(v, "result");
                return r;
            }
        } catch (const Failure &e) {
            if (e.status != 404)
                throw;
        }
    }
    const auto previous_temp = str(v, "temporary");
    if (!previous_temp.empty()) {
        const auto storage = mapped(path);
        discard_upload_temporary(storage.root, storage.path, previous_temp);
    }
    auto pending = begin_upload(effective);
    v["temporary"] = pending->temporary_name();
    auto spool = File::open(state_.root(), "/resume-" + id);
    struct Commit {
        Request request;
        std::shared_ptr<Upload> upload;
        std::shared_ptr<File> spool;
        json::object metadata;
        std::uint64_t offset = 0, total = 0;
        Sha256 piece;
        std::string chain;
        std::shared_ptr<void> activity;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    auto job = std::make_shared<Commit>();
    job->request = effective;
    job->upload = pending;
    job->spool = spool;
    job->metadata = v;
    job->total = size;
    job->activity = activity;
    job->cancel = std::make_shared<std::atomic<bool>>(false);
    v["phase"] = "committing";
    state_.put("resume", id, json::serialize(v));
    Response response;
    response.cancel = [flag = job->cancel] { flag->store(true); };
    response.deferred = [this, job, id](std::function<void(Response)> done) {
        auto step = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weak = step;
        *step = [this, job, id, done, weak] {
            auto continuation = weak.lock();
            if (!continuation)
                return;
            if (!copy_workers_.submit([this, job, id, done, continuation] {
                    try {
                        if (job->cancel->load())
                            throw Failure(499, "Upload publication paused; retry commit to continue");
                        auto block = cache_.transfer_buffer();
                        std::size_t budget = 4u << 20;
                        while (job->offset < job->total && budget) {
                            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(
                                std::min(block->size, budget),
                                std::min<std::uint64_t>(job->total - job->offset, 1048576 - job->offset % 1048576)));
                            if (job->spool->read(job->offset, block->data, n) != n)
                                throw Failure(500, "Incomplete durable spool");
                            job->upload->append(block->data, n);
                            job->piece.update(block->data, n);
                            job->offset += n;
                            budget -= n;
                            if (job->offset % 1048576 == 0 || job->offset == job->total) {
                                const auto material = job->chain + job->piece.finish();
                                Sha256 next;
                                next.update(material.data(), material.size());
                                job->chain = next.finish();
                                job->piece = Sha256{};
                            }
                        }
                        transfer_activity(job->request);
                        if (job->offset < job->total) {
                            (*continuation)();
                            return;
                        }
                        if (job->chain != str(job->metadata, "chain"))
                            throw Failure(409, "Durable upload integrity check failed; re-upload the source");
                        auto result = finish_upload(job->request, job->upload);
                        job->metadata["phase"] = "done";
                        job->metadata["expires"] = now() + 600;
                        job->metadata["result"] = result.body;
                        state_.put("resume", id, json::serialize(job->metadata));
                        complete_resume(id, num(job->metadata, "expires"));
                        job->spool.reset();
                        remove_spool(state_, id);
                        job->activity.reset();
                        done(std::move(result));
                    } catch (const Failure &e) {
                        job->activity.reset();
                        done(error(e.status, e.what()));
                    } catch (const std::exception &) {
                        job->activity.reset();
                        done(error(500, "Upload publication failed; durable chunks are retained"));
                    }
                })) {
                job->activity.reset();
                done(error(503, "Storage queue is full; retry commit"));
            }
        };
        (*step)();
    };
    return response;
}
void Service::recycle_path(const Request &req, const std::string &path) {
    protect_config(path, true);
    const auto storage = mapped(path);
    protect_tls_key(storage, true);
    if (remote_mount(path))
        throw Failure(405, "Browser mount deletes do not support host recycle");
    auto entries = state_.scan("trash");
    if (entries.size() >= 4096)
        throw Failure(507, "Recycle entry capacity reached");
    std::uint64_t used = 0;
    for (const auto &e : entries) {
        const auto n = num(object(e.second), "size");
        if (n > UINT64_MAX - used)
            throw Failure(507, "Recycle capacity overflow");
        used += n;
    }
    const auto size = tree_size(storage.root, storage.path, config_.directory_limit);
    if (size > settings()->recycle_bytes || used > settings()->recycle_bytes - size)
        throw Failure(507, "Recycle bin is full; empty it or increase its capacity");
    const auto id = random_token();
    config_store_.depend(storage.root/".hls-trash",true);
    const auto root = trash_root(storage.root);
    json::object v{{"path", path},      {"root", storage.root.u8string()},
                   {"ip", req.peer_ip}, {"size", size},
                   {"time", now()},     {"phase", "pending"}};
    state_.put("trash", id, json::serialize(v));
    secure_relocate(storage.root, storage.path, root, "/" + id);
    v["phase"] = "ready";
    state_.put("trash", id, json::serialize(v));
}
Response Service::recycle_api(const Request &req, const std::string &route) {
    browser_guard(req, req.method != "GET");
    auto records = state_.scan("trash");
    if (req.method == "GET" && route == "/api/recycle") {
        json::array list;
        const auto offset = param(req, "offset").empty() ? 0 : parse_unsigned(param(req, "offset"), 4096),
                   limit = param(req, "limit").empty() ? 200 : parse_unsigned(param(req, "limit"), 500);
        std::size_t total = 0;
        for (const auto &e : records) {
            auto o = object(e.second);
            const auto path = str(o, "path");
            if (!req.local_admin &&
                (settings()->write_mode == "isolated" ? !within(owner_root(req), path) : str(o, "ip") != req.peer_ip))
                continue;
            if (total++ < offset || list.size() >= limit)
                continue;
            o.erase("root");
            o["id"] = e.first;
            list.push_back(std::move(o));
        }
        return encoded(
            {{"entries", std::move(list)}, {"enabled", settings()->recycle}, {"total", total}, {"offset", offset}});
    }
    if (req.method != "POST")
        throw Failure(405, "Use POST");
    const auto id = param(req, "id");
    if (!id_ok(id))
        throw Failure(400, "Invalid recycle id");
    std::lock_guard<std::mutex> l(write_mutex_);
    const auto item = records.find(id);
    if (item == records.end())
        throw Failure(404, "Recycle entry not found");
    const auto o = object(item->second);
    const auto path = str(o, "path");
    if (!req.local_admin &&
        (settings()->write_mode == "isolated" ? !within(owner_root(req), path) : str(o, "ip") != req.peer_ip))
        throw Failure(403, "Recycle entry belongs to another client");
    const auto source = std::filesystem::u8path(str(o, "root"));
    if (mapped(path).root != source)
        throw Failure(409, "Original mount changed");
    const auto root = trash_root(source);
    if (route == "/api/recycle/restore") {
        const auto requested = param(req, "to");
        const auto to = requested.empty() ? path : normalize_path(requested);
        authorize(req, to);
        check_busy(to);
        script_file(req, "restore", to, num(o, "size"));
        upload_parents(req, to, true);
        const auto dst = mapped(to);
        if (dst.root != source)
            throw Failure(409, "Restore must stay on its original volume");
        secure_relocate(root, "/" + id, dst.root, dst.path);
        changed();
        state_.erase("trash", id);
        return encoded({{"path", to}, {"restored", true}});
    }
    if (route != "/api/recycle/purge")
        throw Failure(404, "Unknown recycle action");
    secure_remove(root, "/" + id, true, config_.directory_limit);
    state_.erase("trash", id);
    return encoded({{"ok", true}});
}
void Service::maintain_durable() {
    static constexpr std::uint64_t interval = 60;
    const auto current = now();
    const auto old = state_.get("maintenance", "last");
    if (!old.empty() && parse_unsigned(old, UINT64_MAX) + interval > current)
        return;
    state_.put("maintenance", "last", std::to_string(current));
    unsigned budget = 16;
    for (const auto &e : state_.scan("resume")) {
        if (!budget)
            break;
        const auto v = object(e.second);
        if (num(v, "expires") >= current)
            continue;
        auto guard = resume_lock(e.first);
        bool available = false;
        if (!guard->busy.compare_exchange_strong(available, true))
            continue;
        auto activity = std::shared_ptr<void>(reinterpret_cast<void *>(1), [guard](void *) { guard->busy = false; });
        const auto temporary = str(v, "temporary"), path = str(v, "path");
        if (!temporary.empty()) {
            try {
                discard_upload_temporary(std::filesystem::u8path(str(v, "root")), str(v, "storage", path), temporary);
            } catch (const Failure &error) {
                if (error.status != 404)
                    continue;
            }
        }
        remove_spool(state_, e.first);
        state_.erase("resume", e.first);
        {
            std::lock_guard<std::mutex> summary(resume_mutex_);
            resume_summary_.erase(e.first);
        }
        --budget;
    }
    for (const auto &e : state_.scan("trash")) {
        if (!budget)
            break;
        const auto o = object(e.second);
        const auto root = std::filesystem::u8path(str(o, "root"));
        const auto trash = root / ".hls-trash";
        if (link_like(trash))
            continue;
        const auto target = trash / e.first;
        std::error_code ec;
        const auto exists = std::filesystem::exists(target, ec);
        if (str(o, "phase") == "pending" && !exists) {
            state_.erase("trash", e.first);
            continue;
        }
        if (num(o, "time") + settings()->recycle_days * 86400ull >= current)
            continue;
        std::unique_lock<std::mutex> l(write_mutex_, std::try_to_lock);
        if (!l.owns_lock())
            continue;
        try {
            secure_remove(trash, "/" + e.first, true, config_.directory_limit);
            state_.erase("trash", e.first);
            --budget;
        } catch (const Failure &error) {
            if (error.status == 404)
                state_.erase("trash", e.first);
        }
    }
}
Response Service::durable_api(const Request &req, const std::string &route) {
    browser_guard(req, req.method != "GET");
    if (route == "/api/admin/state") {
        if (!req.local_admin)
            throw Failure(403, "Host access required");
        if (req.method == "POST") {
            const auto o = object(req.body);
            const auto action = str(o, "action"), ip = str(o, "ip");
            boost::system::error_code ec;
            const auto address = boost::asio::ip::make_address(ip, ec);
            if (ec)
                throw Failure(400, "Expected IP address");
            if (action != "reset-quota")
                throw Failure(400, "Unknown state action");
            std::lock_guard<std::mutex> l(quota_->mutex);
            if (quota_->reserved[address.to_string()])
                throw Failure(409, "Wait for active uploads before resetting quota");
            state_.erase("quota", address.to_string());
            quota_->used.erase(address.to_string());
        } else if (req.method != "GET")
            throw Failure(405, "Use GET or POST");
        const auto offset = param(req, "offset").empty() ? 0 : parse_unsigned(param(req, "offset"), 65536),
                   limit = param(req, "limit").empty() ? 256 : parse_unsigned(param(req, "limit"), 512);
        std::size_t total = 0;
        json::array quotas;
        {
            std::lock_guard<std::mutex> l(quota_->mutex);
            for (const auto &e : quota_->used) {
                if (total++ < offset || quotas.size() >= limit)
                    continue;
                quotas.push_back(json::object{{"ip", e.first},
                                              {"used", e.second},
                                              {"reserved", quota_->reserved[e.first]},
                                              {"limit", settings()->upload_limit(e.first)}});
            }
        }
        return encoded({{"quotas", std::move(quotas)},
                        {"total", total},
                        {"offset", offset},
                        {"uploads", state_.scan("resume").size()},
                        {"recycleEntries", state_.scan("trash").size()},
                        {"durable", true}});
    }
    if (req.method != "POST")
        throw Failure(405, "Use POST");
    const auto o = object(req.body);
    const auto *items = o.if_contains("items");
    if (!items || !items->is_array() || items->as_array().empty() || items->as_array().size() > 256)
        throw Failure(400, "Batch size must be 1-256");
    const auto action = str(o, "action"), policy = str(o, "conflict", "skip");
    if (action != "delete" && action != "move" && action != "mkdir")
        throw Failure(400, "Batch action must be delete, move or mkdir");
    if (policy != "skip" && policy != "replace" && policy != "keep-both")
        throw Failure(400, "Unknown conflict policy");
    std::vector<std::pair<std::string, std::string>> paths;
    std::vector<std::string> replacements;
    for (const auto &item : items->as_array()) {
        if (!item.is_object())
            throw Failure(400, "Expected batch item object");
        const auto &v = item.as_object();
        const auto path = normalize_path(str(v, "path")), to = action == "move" ? normalize_path(str(v, "to")) : "";
        authorize(req, path);
        if (remote_mount(path) || (!to.empty() && remote_mount(to)))
            throw Failure(405, "Use provider operations for browser-mounted entries");
        protect_config(path, true);
        if (!to.empty()) {
            authorize(req, to);
            protect_config(to, true);
        }
        paths.emplace_back(path, to);
        const auto replacement = str(v, "replaceVersion");
        if (replacement.size() > 512)
            throw Failure(400, "Invalid replacement version");
        replacements.push_back(replacement);
    }
    json::array results;
    std::lock_guard<std::mutex> l(write_mutex_);
    std::size_t item_index = 0;
    for (auto pair : paths) {
        const auto replacement = replacements[item_index++];
        json::object result{{"path", pair.first}};
        try {
            authorize(req, pair.first);
            check_busy(pair.first);
            script_file(req, action, pair.first);
            if (action == "delete") {
                if (settings()->recycle)
                    recycle_path(req, pair.first);
                else
                    remove_path(pair.first, true);
                move_attention(pair.first);
            } else if (action == "mkdir")
                mkdir_path(pair.first);
            else {
                auto to = pair.second;
                authorize(req, to);
                check_busy(to);
                bool replaced = false;
                bool exists = false;
                try {
                    resolve(to);
                    exists = true;
                } catch (const Failure &e) {
                    if (e.status != 404)
                        throw;
                }
                if (exists) {
                    if (policy == "skip") {
                        result["skipped"] = true;
                        result["status"] = 200;
                        results.push_back(std::move(result));
                        continue;
                    }
                    if (policy == "keep-both") {
                        const auto parent = std::filesystem::u8path(to).parent_path().generic_u8string();
                        const auto name = std::filesystem::u8path(to).filename();
                        const auto stem = name.stem().u8string(), ext = name.extension().u8string();
                        for (unsigned n = 1; n <= 10000; ++n) {
                            auto candidate =
                                (parent == "/" ? "" : parent) + "/" + stem + " (" + std::to_string(n) + ")" + ext;
                            try {
                                resolve(candidate);
                            } catch (const Failure &e) {
                                if (e.status == 404) {
                                    to = candidate;
                                    exists = false;
                                    break;
                                }
                                throw;
                            }
                        }
                        if (exists)
                            throw Failure(409, "Cannot allocate a unique filename");
                        authorize(req, to);
                    } else {
                        if (replacement.empty())
                            throw Failure(409, "Confirm the destination version before replacing");
                        const auto a = mapped(pair.first), b = mapped(to);
                        if (a.root != b.root)
                            throw Failure(409, "Move cannot cross mounts");
                        const auto src_version = open_file(pair.first)->info().version,
                                   dst_version = open_file(to)->info().version;
                        if (dst_version != replacement)
                            throw Failure(409, "Destination changed after confirmation");
                        secure_replace_move(a.root, a.path, b.path, src_version, dst_version);
                        replaced = true;
                    }
                }
                if (!replaced)
                    rename_path(pair.first, to);
                move_attention(pair.first, to);
                result["to"] = to;
            }
            result["status"] = 200;
        } catch (const Failure &e) {
            result["status"] = e.status;
            result["error"] = e.what();
        } catch (const std::exception &) {
            result["status"] = 500;
            result["error"] = "Batch operation failed";
        }
        results.push_back(std::move(result));
    }
    changed();
    return encoded({{"results", std::move(results)}});
}
} // namespace hls
