// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config.hpp"
#include <mutex>
#include <map>
#include <set>
struct sqlite3;
namespace hls {
class StateStore {
  public:
    explicit StateStore(const Config &config);
    ~StateStore();
    StateStore(const StateStore &) = delete;
    std::string get(const std::string &scope, const std::string &key);
    std::map<std::string, std::string> scan(const std::string &scope);
    void put(const std::string &scope, const std::string &key, const std::string &value);
    void transfer_checkpoint(const std::string& key,const std::string& metadata,const std::string& journal);
    void erase(const std::string &scope, const std::string &key);
    const std::filesystem::path &root() const { return root_; }
    bool protects(const std::filesystem::path &candidate, bool ancestor = false) const;
    void protect() const;

  private:
    std::filesystem::path root_;
    std::set<std::string> identities_;
    sqlite3 *db_ = nullptr;
    std::intptr_t lock_ = -1;
    std::mutex mutex_;
};
} // namespace hls
