// SPDX-License-Identifier: Apache-2.0
#include "script_policy.hpp"
#include "service.hpp"
#include "util.hpp"
#include <boost/json.hpp>
#include <sstream>
namespace hls {
namespace {
namespace json = boost::json;
json::object object(const std::string &text) {
    boost::system::error_code ec;
    json::parse_options options;
    options.max_depth = 16;
    auto value = json::parse(text, ec, {}, options);
    if (ec || !value.is_object())
        throw Failure(500, "Script must return an object");
    return value.as_object();
}
std::string field(const json::object &o, const char *k, const std::string &fallback = "") {
    const auto *v = o.if_contains(k);
    if (!v)
        return fallback;
    if (!v->is_string())
        throw Failure(500, "Script field must be text");
    return std::string(v->as_string());
}
void headers(const json::object &o, Response &r) {
    const auto *h = o.if_contains("headers");
    if (!h)
        return;
    if (!h->is_object() || h->as_object().size() > 32)
        throw Failure(500, "Invalid script headers");
    for (const auto &v : h->as_object()) {
        if (!v.value().is_string() || !safe_response_header(std::string(v.key()), std::string(v.value().as_string())))
            throw Failure(500, "Unsafe script response header");
        r.headers.emplace_back(std::string(v.key()), std::string(v.value().as_string()));
    }
}
} // namespace
void Service::script_file(const Request &req, const std::string &op, const std::string &path, std::uint64_t size) {
    if (!scripts_.enabled())
        return;
    auto result = object(scripts_.invoke("onFileOperation", json::serialize(json::object{{"operation", op},
                                                                                         {"path", path},
                                                                                         {"size", size},
                                                                                         {"ip", req.peer_ip},
                                                                                         {"host", req.local_admin},
                                                                                         {"mode", config_.mode}})));
    if (const auto *allow = result.if_contains("allow")) {
        if (!allow->is_bool())
            throw Failure(500, "Script allow must be boolean");
        if (!allow->as_bool())
            throw Failure(403, field(result, "message", "Operation denied by host script"));
    }
}
void Service::application_guard(const Request &req, bool files) {
    if (config_.workspace_app.empty())
        return;
    browser_guard(req, false);
    if (!req.local_admin && !req.local_device) {
        if (config_.workspace_access == "host")
            throw Failure(403, "Application accepts host clients only");
        if (!config_.workspace_allow_ips.empty()) {
            bool allowed = false;
            for (const auto &rule : config_.workspace_allow_ips)
                allowed = allowed || matches_ip_rule(req.peer_ip, rule);
            if (!allowed)
                throw Failure(403, "Application client address is not allowed");
        }
        if (files && !config_.workspace_share)
            throw Failure(403, "Application data and file manager are private");
    }
    if (files && req.application_listener && !config_.workspace_share)
        throw Failure(403, "Application data and file manager are private");
}
bool Service::script_request(const Request &req, Response &response) {
    if (!scripts_.enabled())
        return false;
    browser_guard(req, false);
    if (!config_.workspace_app.empty() && req.control_channel)
        return false;
    if (!config_.workspace_app.empty() && req.method != "GET" && req.method != "HEAD") {
        if (req.header("origin").empty() && req.header("x-hls-app") != "1")
            throw Failure(403, "Application mutations require a same-origin Origin or X-HLS-App: 1");
        const auto site = lower_ascii(req.header("sec-fetch-site"));
        if (site == "cross-site")
            throw Failure(403, "Cross-site application mutation denied");
    }
    json::object hs;
    for (const auto &h : req.headers)
        if (h.first != "cookie" && h.first != "authorization" && h.first != "x-hls-token")
            hs[h.first] = h.second;
    const auto out =
        object(scripts_.invoke("onRequest", json::serialize(json::object{{"method", req.method},
                                                                         {"url", req.target},
                                                                         {"body", req.body},
                                                                         {"ip", req.peer_ip},
                                                                         {"host", req.local_admin || req.local_device},
                                                                         {"secure", req.secure},
                                                                         {"mode", config_.mode},
                                                                         {"headers", std::move(hs)}})));
    if (!config_.workspace_app.empty() &&
        (req.method == "POST" || req.method == "PUT" || req.method == "PATCH" || req.method == "DELETE"))
        changed();
    if (out.empty()) {
        if (scripts_.asset(normalize_path(url_decode(req.target.substr(0, req.target.find('?')))), response.body,
                           response.content_type)) {
            if (req.method != "GET" && req.method != "HEAD")
                throw Failure(405, "Workspace assets require GET or HEAD");
            return true;
        }
        if (!config_.workspace_app.empty()) {
            response = error(404, "Application resource not found");
            return true;
        }
        return false;
    }
    if (const auto *allow = out.if_contains("allow"))
        if (!allow->is_bool() || !allow->as_bool())
            throw Failure(403, field(out, "message", "Request denied by host script"));
    if (const auto *writing = out.if_contains("write")) {
        if (!writing->is_object() || (req.method != "POST" && req.method != "PUT"))
            throw Failure(405, "Script writes require POST or PUT");
        const auto &w = writing->as_object();
        const auto path = normalize_path(field(w, "path")), bytes = field(w, "text");
        auto upload = req;
        upload.target =
            "/api/upload?path=" + url_encode(path) + "&replaceVersion=" + url_encode(field(w, "replaceVersion"));
        upload.headers["content-length"] = std::to_string(bytes.size());
        auto pending = begin_upload(upload);
        pending->append(bytes.data(), bytes.size());
        response = finish_upload(upload, pending);
        headers(out, response);
        return true;
    }
    const auto file = field(out, "file");
    if (!file.empty()) {
        if (!config_.workspace_app.empty() && config_.script_fs == "off")
            throw Failure(403, "Application filesystem is disabled");
        if (req.method != "GET" && req.method != "HEAD")
            throw Failure(405, "Script file responses require GET or HEAD");
        response = download(req, normalize_path(file), false);
        headers(out, response);
        return true;
    }
    const auto rewrite = field(out, "rewrite");
    if (!rewrite.empty()) {
        if (!config_.workspace_app.empty())
            throw Failure(403,
                          "Workspace rewrites cannot expose the host filesystem; return a permitted file explicitly");
        if (req.method != "GET" && req.method != "HEAD")
            throw Failure(405, "Script rewrites require GET or HEAD");
        const auto path = normalize_path(rewrite);
        if (path.rfind("/api", 0) == 0 || path.rfind("/_hls", 0) == 0 || path == "/mcp")
            throw Failure(403, "Scripts cannot rewrite to control routes");
        response = server_file(req, path);
        headers(out, response);
        return true;
    }
    if (!out.if_contains("body") && !out.if_contains("status")) {
        if (!config_.workspace_app.empty()) {
            response = error(404, "Application resource not found");
            return true;
        }
        return false;
    }
    response.body = field(out, "body");
    response.content_type = field(out, "contentType", "text/plain; charset=utf-8");
    if (!safe_response_header("Content-Type", response.content_type))
        throw Failure(500, "Invalid script content type");
    if (const auto *status = out.if_contains("status")) {
        if (!status->is_int64() || status->as_int64() < 200 || status->as_int64() > 599)
            throw Failure(500, "Invalid script HTTP status");
        response.status = static_cast<unsigned>(status->as_int64());
    }
    headers(out, response);
    return true;
}
Response Service::server_file(const Request &req, const std::string &path) {
    if (req.method != "GET" && req.method != "HEAD")
        return error(405, "Use GET or HEAD, or configure a request script");
    try {
        const auto resolved = resolve(path);
        auto effective = req;
        Response entry;
        if (path == "/" && settings()->hotlink_seconds) {
            browser_guard(req, false);
            const auto id = new_access(req, entry, !settings()->login_password.empty());
            effective.headers["cookie"] = "HLS_SID=" + id;
        }
        auto serve = [&](const std::string &file) {
            auto result = download(effective, file, false);
            result.headers.insert(result.headers.end(), entry.headers.begin(), entry.headers.end());
            return result;
        };
        if (std::filesystem::is_directory(resolved)) {
            const auto raw = req.target.substr(0, req.target.find('?'));
            if (raw.empty() || raw.back() != '/') {
                Response r;
                r.status = 301;
                r.headers.emplace_back(
                    "Location",
                    raw + "/" +
                        (req.target.find('?') == std::string::npos ? "" : req.target.substr(req.target.find('?'))));
                return r;
            }
            std::istringstream docs(config_.default_documents);
            std::string name;
            while (std::getline(docs, name, ',')) {
                if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
                    throw Failure(400, "Invalid default document");
                try {
                    return serve((path == "/" ? "" : path) + "/" + name);
                } catch (const Failure &e) {
                    if (e.status != 404)
                        throw;
                }
            }
            throw Failure(404, "Default document not found");
        }
        return serve(path);
    } catch (const Failure &e) {
        if (e.status == 404 && !config_.spa_fallback.empty() && std::filesystem::u8path(path).extension().empty() &&
            req.header("accept").find("text/html") != std::string::npos) {
            return download(req, normalize_path(config_.spa_fallback), false);
        }
        const auto found = config_.error_pages.find(std::to_string(e.status));
        if (found != config_.error_pages.end()) {
            try {
                auto response = download(req, normalize_path(found->second), false);
                response.status = e.status;
                response.headers.emplace_back("Cache-Control", "no-store");
                return response;
            } catch (const Failure &) {
            }
        }
        return error(e.status, e.what());
    }
}
} // namespace hls
