// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#pragma once
#include "live_settings.hpp"
#include "platform.hpp"
#include <mutex>
#include <set>

namespace hls {
// JSON is converted to the same validated option parser as the CLI. CLI tokens
// are appended last; paths in JSON are relative to the configuration directory.
std::vector<std::string> config_arguments(Config& config,int argc,char** argv);
class ConfigStore {
public:
    explicit ConfigStore(const Config& config,bool lock=true);
    ~ConfigStore();
    bool enabled()const{return persistent_;}
    bool exists()const;
    void save(const Config& config,const LiveSettings& live);
    bool frozen(const std::filesystem::path& path)const;
    void protect(const std::filesystem::path& path,bool descendants=false,std::size_t limit=100000)const;
    void protect(const File& file)const;
    std::string basename()const{return path_.filename().u8string();}
    struct Dependency {std::filesystem::path path;bool directory=false;};
    void depend(const std::filesystem::path& path,bool directory=false);
    std::vector<Dependency> dependencies()const;
    bool dependency_path(const std::filesystem::path& path)const;
private:
    std::filesystem::path path_;
    bool persistent_=false,lock_enabled_=true;
    mutable std::mutex mutex_;
    std::set<std::string> retired_;
    std::set<std::string> dependency_ids_;
    std::vector<Dependency> dependencies_;
    std::string identity_;
    std::intptr_t lock_handle_=-1;
    void acquire_lock();
    bool frozen_unlocked(const std::filesystem::path& path)const;
};
}
