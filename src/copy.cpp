// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#include "service.hpp"
#include "util.hpp"
#include <algorithm>

namespace hls {
Response Service::copy_file(const Request &request) {
    browser_guard(request, true);
    const auto p = query_params(request.target);
    const auto copy = p.find("copy");
    const auto id = copy == p.end() ? random_token() : copy->second;
    if (id.size() < 16 || id.size() > 128 || !std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        }))
        throw Failure(400, "Invalid copy identifier");
    if (request.target.substr(0, request.target.find('?')) == "/api/copy-progress") {
        std::lock_guard<std::mutex> lock(write_mutex_);
        const auto job = copy_jobs_.find(id);
        if (job == copy_jobs_.end())
            throw Failure(404, "Copy is no longer active");
        if (!request.local_admin && job->second.peer != request.peer_ip)
            throw Failure(403, "Copy belongs to another client");
        Response response;
        response.headers.emplace_back("Cache-Control", "no-store");
        response.body = "{\"done\":" + std::to_string(job->second.progress->load()) +
                        ",\"total\":" + std::to_string(job->second.total) + "}";
        return response;
    }
    if (request.target.substr(0, request.target.find('?')) == "/api/copy-cancel") {
        std::lock_guard<std::mutex> lock(write_mutex_);
        const auto job = copy_jobs_.find(id);
        if (job != copy_jobs_.end()) {
            if (!request.local_admin && job->second.peer != request.peer_ip)
                throw Failure(403, "Copy belongs to another client");
            if (auto flag = job->second.cancelled.lock())
                flag->store(true);
        }
        Response response;
        response.body = "{\"ok\":true}";
        return response;
    }
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    auto progress = std::make_shared<std::atomic<std::uint64_t>>(0);
    const auto from_it = p.find("path"), to_it = p.find("to");
    if (from_it == p.end() || to_it == p.end())
        throw Failure(400, "Expected source and destination");
    const auto from = normalize_path(from_it->second), to = normalize_path(to_it->second);
    if (from == to)
        throw Failure(409, "Choose a different destination name");
    std::shared_ptr<File> source;
    std::shared_ptr<Upload> upload;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        authorize(request, to);
        check_busy(to);
        script_file(request, "copy", to);
        for (auto job = copy_jobs_.begin(); job != copy_jobs_.end();)
            if (job->second.cancelled.expired())
                job = copy_jobs_.erase(job);
            else
                ++job;
        if (copy_jobs_.size() >= 128)
            throw Failure(503, "Copy job capacity reached");
        if (copy_jobs_.count(id))
            throw Failure(409, "Copy identifier is already active");
        if (remote_mount(to))
            throw Failure(405, "Use the browser provider copy operation");
        script_file(request, "read", from);
        source = open_file(from);
        const auto version = p.find("version");
        if (version != p.end() && version->second != source->info().version)
            throw Failure(409, "Source changed before copying");
        if (source->info().size > settings()->max_upload_bytes)
            throw Failure(413, "Copy exceeds the per-file write limit");
        auto accounting = request;
        accounting.headers["content-length"] = std::to_string(source->info().size);
        preflight_quota(accounting);
        auto charge = upload_charge(accounting);
        upload_parents(request, to, true);
        const auto destination = mapped(to);
        upload =
            std::make_shared<Upload>(destination.root, destination.path, settings()->max_upload_bytes,
                                     p.count("replaceVersion") ? p.at("replaceVersion") : "", to, std::move(charge));
        uploads_[to] = upload;
        copy_jobs_[id] = {request.peer_ip, cancelled, progress, source->info().size};
    }
    Response response;
    response.cancel = [cancelled] { cancelled->store(true); };
    auto effective = request;
    if (copy == p.end())
        effective.target += (effective.target.find('?') == std::string::npos ? "?" : "&") + std::string("copy=") + id;
    response.deferred = [this, effective, source, upload, cancelled, progress, id](std::function<void(Response)> done) {
        auto offset = std::make_shared<std::uint64_t>(0);
        auto step = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weak = step;
        auto finish = [this, id, done](Response result) {
            {
                std::lock_guard<std::mutex> lock(write_mutex_);
                copy_jobs_.erase(id);
            }
            done(std::move(result));
        };
        *step = [this, effective, source, upload, cancelled, progress, offset, weak, finish] {
            auto continuation = weak.lock();
            if (!continuation)
                return;
            if (!copy_workers_.submit([this, effective, source, upload, cancelled, progress, offset, continuation, finish] {
                    try {
                        if (cancelled->load())
                            throw Failure(499, "Copy cancelled");
                        auto block = cache_.transfer_buffer();
                        std::size_t budget = 4u << 20;
                        while (*offset < source->info().size && budget) {
                            const auto wanted = static_cast<std::size_t>(
                                std::min<std::uint64_t>(std::min(block->size, budget), source->info().size - *offset));
                            if (source->read(*offset, block->data, wanted) != wanted)
                                throw Failure(409, "Source changed during copy");
                            upload->append(block->data, wanted);
                            *offset += wanted;
                            progress->store(*offset);
                            budget -= wanted;
                        }
                        transfer_activity(effective);
                        if (*offset < source->info().size) {
                            (*continuation)();
                            return;
                        }
                        if (!source->unchanged())
                            throw Failure(409, "Source changed during copy");
                        if (cancelled->load())
                            throw Failure(499, "Copy cancelled");
                        finish(finish_upload(effective, upload));
                    } catch (const Failure &e) {
                        finish(error(e.status, e.what()));
                    } catch (const std::exception &) {
                        finish(error(500, "Copy failed"));
                    }
                }))
                finish(error(503, "Storage queue is full"));
        };
        (*step)();
    };
    return response;
}
} // namespace hls
