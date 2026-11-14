// SPDX-License-Identifier: Apache-2.0
#include "runtime_files.hpp"
#include "platform.hpp"
#include <mutex>
#include <vector>
namespace hls {
struct RuntimeFile::Entry {
    std::filesystem::path path;
    std::string identity;
};
namespace {
struct Active {
    std::filesystem::path path;
    std::string identity;
    const void *owner;
};
std::mutex mutex;
std::vector<Active> active;
} // namespace
RuntimeFile::RuntimeFile(const std::filesystem::path &path) : entry_(std::make_shared<Entry>()) {
    entry_->path = std::filesystem::absolute(path).lexically_normal();
    std::lock_guard<std::mutex> lock(mutex);
    if (active.size() >= 16384)
        throw Failure(503, "Active transaction protection capacity reached");
    active.push_back({entry_->path, {}, entry_.get()});
}
void RuntimeFile::opened() {
    std::string identity;
    try {
        identity = file_identity(entry_->path);
    } catch (const std::exception &) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &value : active)
        if (value.owner == entry_.get()) {
            value.identity = identity;
            return;
        }
}
RuntimeFile::~RuntimeFile() {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = active.begin(); it != active.end(); ++it)
        if (it->owner == entry_.get()) {
            active.erase(it);
            break;
        }
}
bool runtime_file_frozen(const std::filesystem::path &path) {
    const auto real = std::filesystem::absolute(path).lexically_normal();
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &value : active)
        if (path_within(value.path, real))
            return true;
    return false;
}
bool runtime_file_identity(const std::string &identity) {
    if (identity.empty())
        return false;
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &value : active)
        if (!value.identity.empty() && value.identity == identity)
            return true;
    return false;
}
} // namespace hls
