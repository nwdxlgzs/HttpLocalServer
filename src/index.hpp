#pragma once
#include "config.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct sqlite3;
namespace hls {
class ConfigStore;
struct DirectoryEntry {
    std::string path, name;
    bool directory = false;
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    bool hidden = false;
};
struct SearchResult { std::vector<DirectoryEntry> entries; std::size_t total = 0; bool indexing = false, truncated = false; };
class SearchIndex {
public:
    SearchIndex(const Config& config,const ConfigStore& protected_files);
    ~SearchIndex();
    void start();
    void stop();
    void refresh();
    SearchResult search(const std::string& query, const std::string& directory, std::size_t offset, std::size_t limit, bool show_hidden=false);
    bool scanning() const { return scanning_.load(); }
    std::size_t count() const { return count_.load(); }
    bool truncated() const { return truncated_.load(); }
    bool enabled() const { return config_.index; }
    bool failed() const { return failed_.load(); }
private:
    struct Snapshot { sqlite3* db = nullptr; std::mutex mutex; ~Snapshot(); };
    void scan();
    Config config_;
    const ConfigStore& protected_files_;
    std::mutex mutex_; std::condition_variable wake_; std::shared_ptr<Snapshot> snapshot_;
    std::atomic<bool> stopping_{false}, scanning_{false}, truncated_{false}, failed_{false};
    bool dirty_=false;
    std::atomic<std::size_t> count_{0}; std::thread thread_;
};
std::int64_t file_modified(const std::filesystem::directory_entry& entry);
std::string entry_json(const DirectoryEntry& entry);
}
